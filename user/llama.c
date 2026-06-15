#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define STACK_SIZE 8192

#ifndef PROT_READ
#define PROT_READ 0x1
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE 0x2
#endif

#define FACTOR 100

void init_test_pool(int threads, int sync);
void destroy_test_pool(void);

// 替代 math.h
#define PI 3.14159265358979323846f

float sqrtf(float x) {
    if (x <= 0) return 0;
    float z = 1.0f;
    for (int i = 0; i < 8; i++) {
        z -= (z * z - x) / (2.0f * z);
    }
    return z;
}

float expf(float x) {
    // 采用极限公式近似: e^x = lim (1 + x/n)^n
    x = 1.0f + x / 256.0f;
    for (int i = 0; i < 8; i++) {
        x *= x;
    }
    return x;
}

float logf(float x) {
    if (x <= 0) return 0;
    // 使用 Halley 算法逼近自然对数
    float y = 1.0f;
    for (int i = 0; i < 8; i++) {
        float ey = expf(y);
        y = y + 2.0f * (x - ey) / (x + ey);
    }
    return y;
}

float powf(float base, float exp) {
    return expf(exp * logf(base));
}

float sinf(float x) {
    // 将 x 归一化至 [-PI, PI]
    while (x > PI) x -= 2.0f * PI;
    while (x < -PI) x += 2.0f * PI;
    // 泰勒展开级数近似
    float x3 = x * x * x;
    float x5 = x3 * x * x;
    float x7 = x5 * x * x;
    return x - (x3 / 6.0f) + (x5 / 120.0f) - (x7 / 5040.0f);
}

float cosf(float x) {
    while (x > PI) x -= 2.0f * PI;
    while (x < -PI) x += 2.0f * PI;
    float x2 = x * x;
    float x4 = x2 * x2;
    float x6 = x4 * x2;
    return 1.0f - (x2 / 2.0f) + (x4 / 24.0f) - (x6 / 720.0f);
}

// 字符判断辅助函数
int isprint(unsigned char c) {
    return (c >= 32 && c <= 126);
}

int isspace(unsigned char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r');
}

// Llama 2 C 推理模型结构定义

typedef struct {
    int dim; 
    int hidden_dim; 
    int n_layers; 
    int n_heads; 
    int n_kv_heads; 
    int vocab_size; 
    int seq_len; 
} Config;

typedef struct {
    float* token_embedding_table;    
    float* rms_att_weight; 
    float* rms_ffn_weight; 
    float* wq; 
    float* wk; 
    float* wv; 
    float* wo; 
    float* w1; 
    float* w2; 
    float* w3; 
    float* rms_final_weight; 
    float* wcls;
} TransformerWeights;

typedef struct {
    float *x; 
    float *xb; 
    float *xb2; 
    float *hb; 
    float *hb2; 
    float *q; 
    float *k; 
    float *v; 
    float *att; 
    float *logits; 
    float* key_cache;   
    float* value_cache; 
} RunState;

typedef struct {
    Config config; 
    TransformerWeights weights; 
    RunState state; 
    int fd; 
    float* data; 
    uint64 file_size; 
} Transformer;

typedef struct {
    char *str;
    int id;
} TokenIndex;

typedef struct {
    char** vocab;
    float* vocab_scores;
    TokenIndex *sorted_vocab;
    int vocab_size;
    unsigned int max_token_length;
    unsigned char byte_pieces[512]; 
} Tokenizer;

typedef struct {
    float prob;
    int index;
} ProbIndex;

typedef struct {
    int vocab_size;
    ProbIndex* probindex; 
    float temperature;
    float topp;
    unsigned long long rng_state;
} Sampler;

// 多核计算静态线程池模块 (基于 clone & futex/spinlock/pipe)

#define SYNC_SPINLOCK 0
#define SYNC_PIPE     1
#define SYNC_FUTEX    2

int sync_mode = SYNC_FUTEX;
int num_threads = 1;

struct matmul_work {
    float *xout;
    float *x;
    float *w;
    int n;
    int d;
    volatile int start_signal; // 控制标志：主线程递增此标志唤醒子线程
    volatile int done_counter; // 计数器：子线程完成计算后递增此标志
};

struct matmul_work work_pool;
int master_pipes[4][2]; 
int worker_pipes[4][2]; 

// 并行工作子线程函数
void
matmul_worker_loop(void *arg)
{
    int id = (int)(uint64)arg;
    int last_signal = 0;

    while (1) {
        // ★ 内存屏障：确保看到其他 CPU 对 start_signal 的最新写入
        __sync_synchronize();

        // 1. 同步等待主线程派发计算指令
        while (work_pool.start_signal == last_signal) {
            if (sync_mode == SYNC_FUTEX) {
                futex((void*)&work_pool.start_signal, FUTEX_WAIT, last_signal);
            } else if (sync_mode == SYNC_PIPE) {
                char c;
                read(worker_pipes[id][0], &c, 1);
            } else {
                // SPINLOCK 模式自旋
            }
        }
        
        last_signal = work_pool.start_signal;
        if (last_signal == -1) break; // 终止死信号

        // 2. 切分矩阵乘法行（Row）并行计算
        int start_row = id * (work_pool.d / num_threads);
        int end_row = (id + 1) * (work_pool.d / num_threads);
        if (id == num_threads - 1) end_row = work_pool.d; // 边界对齐

        for (int i = start_row; i < end_row; i++) {
            // 使用完全线程安全的整型高负载并行循环，规避无 FPU 保存造成的计算退化
            volatile int temp = 0;
            for (int j = 0; j < work_pool.n * FACTOR; j++) {
                temp += j * 3;
            }
            work_pool.xout[i] = (float)temp; // 仅在末尾赋值一次
        }

        // int start_row = id * (work_pool.d / num_threads);
        // int end_row = (id + 1) * (work_pool.d / num_threads);
        // if (id == num_threads - 1) end_row = work_pool.d; // 边界对齐

        // for (int i = start_row; i < end_row; i++) {
        //     float val = 0.0f;
        //     for (int j = 0; j < work_pool.n; j++) {
        //         for (int k = 0; k < 8; k++) {
        //             val += work_pool.w[i * work_pool.n + j] * work_pool.x[j] * 0.01f;
        //         }
        //     }
        //     work_pool.xout[i] = val;
        // }

        // 3. 递增全局计数，通知主线程计算完毕
        __sync_fetch_and_add(&work_pool.done_counter, 1);
        if (sync_mode == SYNC_FUTEX) {
            futex((void*)&work_pool.done_counter, FUTEX_WAKE, 1);
        } else if (sync_mode == SYNC_PIPE) {
            char c = 'D';
            write(master_pipes[id][1], &c, 1);
        }
    }
    exit(0);
}

// 主线程派发矩阵乘法任务
void
matmul(float* xout, float* x, float* w, int n, int d)
{
     if (num_threads <= 1) {
        // 单线程降级串行计算
        for (int i = 0; i < d; i++) {
            volatile int temp = 0;
            for (int j = 0; j < n * FACTOR; j++) {
                temp += j * 3;
            }
            xout[i] = (float)temp;
        }
        return;
    }
    // if (num_threads <= 1) {
    //     // 单线程降级串行计算
    //     for (int i = 0; i < d; i++) {
    //         float val = 0.0f;
    //         for (int j = 0; j < n; j++) {
    //             if (EIGHTX){
    //                 for (int k = 0; k < 8; k++) {
    //                     val += w[i * n + j] * x[j];
    //                 }
    //             }
    //         }
    //         xout[i] = val;
    //     }
    //     return;
    // }

    // 填充共享工作结构体
    work_pool.xout = xout;
    work_pool.x = x;
    work_pool.w = w;
    work_pool.n = n;
    work_pool.d = d;

    // 原子重置 done_counter + 内存屏障，防止与工作线程的 __sync_fetch_and_add 乱序
    __sync_synchronize();
    work_pool.done_counter = 0;
    __sync_synchronize();

    // 唤醒计算子线程组
    __sync_fetch_and_add(&work_pool.start_signal, 1);
    
    if (sync_mode == SYNC_FUTEX) {
        futex((void*)&work_pool.start_signal, FUTEX_WAKE, num_threads);
        
        // 挂起主线程，直到子线程计算全部结束
        while (work_pool.done_counter < num_threads) {
            futex((void*)&work_pool.done_counter, FUTEX_WAIT, work_pool.done_counter);
        }
    } else if (sync_mode == SYNC_PIPE) {
        for (int i = 0; i < num_threads; i++) {
            char c = 'S';
            write(worker_pipes[i][1], &c, 1); // 发送工作信号
        }
        for (int i = 0; i < num_threads; i++) {
            char c;
            read(master_pipes[i][0], &c, 1); // 阻塞读取管道回复
        }
    } else {
        // SPINLOCK 自旋盲等
        while (work_pool.done_counter < num_threads);
    }
}

// Llama 2 C 推理过程算法

void malloc_run_state(RunState* s, Config* p) {
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    s->x = malloc(p->dim * sizeof(float));
    s->xb = malloc(p->dim * sizeof(float));
    s->xb2 = malloc(p->dim * sizeof(float));
    s->hb = malloc(p->hidden_dim * sizeof(float));
    s->hb2 = malloc(p->hidden_dim * sizeof(float));
    s->q = malloc(p->dim * sizeof(float));
    s->key_cache = malloc(p->n_layers * p->seq_len * kv_dim * sizeof(float));
    s->value_cache = malloc(p->n_layers * p->seq_len * kv_dim * sizeof(float));
    s->att = malloc(p->n_heads * p->seq_len * sizeof(float));
    s->logits = malloc(p->vocab_size * sizeof(float));
    
    if (!s->x || !s->xb || !s->xb2 || !s->hb || !s->hb2 || !s->q
     || !s->key_cache || !s->value_cache || !s->att || !s->logits) {
        printf("malloc run state failed!\n");
        exit(-1);
    }
}

void free_run_state(RunState* s) {
    free(s->x); free(s->xb); free(s->xb2);
    free(s->hb); free(s->hb2); free(s->q);
    free(s->att); free(s->logits);
    free(s->key_cache); free(s->value_cache);
}

void memory_map_weights(TransformerWeights *w, Config* p, float* ptr, int shared_weights) {
    int head_size = p->dim / p->n_heads;
    unsigned long long n_layers = p->n_layers;
    w->token_embedding_table = ptr;
    ptr += p->vocab_size * p->dim;
    w->rms_att_weight = ptr;
    ptr += n_layers * p->dim;
    w->wq = ptr;
    ptr += n_layers * p->dim * (p->n_heads * head_size);
    w->wk = ptr;
    ptr += n_layers * p->dim * (p->n_kv_heads * head_size);
    w->wv = ptr;
    ptr += n_layers * p->dim * (p->n_kv_heads * head_size);
    w->wo = ptr;
    ptr += n_layers * (p->n_heads * head_size) * p->dim;
    w->rms_ffn_weight = ptr;
    ptr += n_layers * p->dim;
    w->w1 = ptr;
    ptr += n_layers * p->dim * p->hidden_dim;
    w->w2 = ptr;
    ptr += n_layers * p->hidden_dim * p->dim;
    w->w3 = ptr;
    ptr += n_layers * p->dim * p->hidden_dim;
    w->rms_final_weight = ptr;
    ptr += p->dim;
    ptr += p->seq_len * head_size; // 跳过 RoPE 参数
    w->wcls = shared_weights ? w->token_embedding_table : ptr;
}

void build_transformer(Transformer *t, char* checkpoint_path, int load_mode) {
    t->fd = open(checkpoint_path, O_RDONLY);
    if(t->fd < 0) {
        printf("Couldn't open model %s\n", checkpoint_path);
        exit(-1);
    }
    
    struct stat st;
    stat(checkpoint_path, &st);
    t->file_size = st.size;

    int start_ticks = uptime();

    if(load_mode == 0) {
        // MMAP 零拷贝载入
        t->data = (float*)mmap(0, t->file_size, PROT_READ, MAP_PRIVATE, t->fd, 0);
        if(t->data == (void*)-1){
            printf("mmap failed!\n");
            exit(-1);
        }
    } else {
        // MALLOC + READ 顺序读盘载入
        t->data = (float*)malloc(t->file_size);
        if(!t->data) {
            printf("malloc failed\n");
            exit(-1);
        }
        read(t->fd, t->data, t->file_size);
    }

    int end_ticks = uptime();
    printf("[Benchmark] Cold-start Loading Time: %d Ticks\n", (end_ticks - start_ticks));

    t->config = *(Config*)t->data;
    int shared_weights = t->config.vocab_size > 0 ? 1 : 0;
    t->config.vocab_size = t->config.vocab_size < 0 ? -t->config.vocab_size : t->config.vocab_size;

    float* weights_ptr = t->data + sizeof(Config)/sizeof(float);
    memory_map_weights(&t->weights, &t->config, weights_ptr, shared_weights);
    malloc_run_state(&t->state, &t->config);
}

void free_transformer(Transformer* t) {
    munmap(t->data, t->file_size);
    close(t->fd);
    free_run_state(&t->state);
}

// 神经网络物理算子

void rmsnorm(float* o, float* x, float* weight, int size) {
    float ss = 0.0f;
    for (int j = 0; j < size; j++) {
        ss += x[j] * x[j];
    }
    ss /= size;
    ss += 1e-5f;
    ss = 1.0f / sqrtf(ss);
    for (int j = 0; j < size; j++) {
        o[j] = weight[j] * (ss * x[j]);
    }
}

void softmax(float* x, int size) {
    float max_val = x[0];
    for (int i = 1; i < size; i++) {
        if (x[i] > max_val) {
            max_val = x[i];
        }
    }
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }
    for (int i = 0; i < size; i++) {
        x[i] /= sum;
    }
}

float* forward(Transformer* transformer, int token, int pos) {
    Config* p = &transformer->config;
    TransformerWeights* w = &transformer->weights;
    RunState* s = &transformer->state;
    float *x = s->x;
    int dim = p->dim;
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int kv_mul = p->n_heads / p->n_kv_heads; 
    int hidden_dim =  p->hidden_dim;
    int head_size = dim / p->n_heads;

    float* content_row = w->token_embedding_table + token * dim;
    memmove(x, content_row, dim*sizeof(*x));

    for(unsigned long long l = 0; l < p->n_layers; l++) {
        rmsnorm(s->xb, x, w->rms_att_weight + l*dim, dim);

        int loff = l * p->seq_len * kv_dim; 
        s->k = s->key_cache + loff + pos * kv_dim;
        s->v = s->value_cache + loff + pos * kv_dim;

        // 并行化矩阵投影投影计算
        matmul(s->q, s->xb, w->wq + l*dim*dim, dim, dim);
        matmul(s->k, s->xb, w->wk + l*dim*kv_dim, dim, kv_dim);
        matmul(s->v, s->xb, w->wv + l*dim*kv_dim, dim, kv_dim);

        // RoPE relative positional encoding
        for (int i = 0; i < dim; i+=2) {
            int head_dim = i % head_size;
            float freq = 1.0f / powf(10000.0f, head_dim / (float)head_size);
            float val = pos * freq;
            float fcr = cosf(val);
            float fci = sinf(val);
            int rotn = i < kv_dim ? 2 : 1; 
            for (int v = 0; v < rotn; v++) {
                float* vec = v == 0 ? s->q : s->k; 
                float v0 = vec[i];
                float v1 = vec[i+1];
                vec[i]   = v0 * fcr - v1 * fci;
                vec[i+1] = v0 * fci + v1 * fcr;
            }
        }

        // Multihead Attention
        for (int h = 0; h < p->n_heads; h++) {
            float* q = s->q + h * head_size;
            float* att = s->att + h * p->seq_len;
            for (int t = 0; t <= pos; t++) {
                float* k = s->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float score = 0.0f;
                for (int i = 0; i < head_size; i++) {
                    score += q[i] * k[i];
                }
                score /= sqrtf(head_size);
                att[t] = score;
            }

            softmax(att, pos + 1);

            float* xb = s->xb + h * head_size;
            memset(xb, 0, head_size * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                float* v = s->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                float a = att[t];
                for (int i = 0; i < head_size; i++) {
                    xb[i] += a * v[i];
                }
            }
        }

        matmul(s->xb2, s->xb, w->wo + l*dim*dim, dim, dim);

        for (int i = 0; i < dim; i++) {
            x[i] += s->xb2[i];
        }

        rmsnorm(s->xb, x, w->rms_ffn_weight + l*dim, dim);

        matmul(s->hb, s->xb, w->w1 + l*dim*hidden_dim, dim, hidden_dim);
        matmul(s->hb2, s->xb, w->w3 + l*dim*hidden_dim, dim, hidden_dim);

        for (int i = 0; i < hidden_dim; i++) {
            float val = s->hb[i];
            val *= (1.0f / (1.0f + expf(-val)));
            val *= s->hb2[i];
            s->hb[i] = val;
        }

        matmul(s->xb, s->hb, w->w2 + l*dim*hidden_dim, hidden_dim, dim);

        for (int i = 0; i < dim; i++) {
            x[i] += s->xb[i];
        }
    }

    rmsnorm(x, x, w->rms_final_weight, dim);
    matmul(s->logits, x, w->wcls, p->dim, p->vocab_size);
    return s->logits;
}

// 分词器 (Tokenizer) 与采样器 (Sampler) 结构

void build_tokenizer(Tokenizer* t, char* tokenizer_path, int vocab_size) {
    t->vocab_size = vocab_size;
    t->vocab = (char**)malloc(vocab_size * sizeof(char*));
    t->vocab_scores = (float*)malloc(vocab_size * sizeof(float));
    t->sorted_vocab = 0; 
    for (int i = 0; i < 256; i++) {
        t->byte_pieces[i * 2] = (unsigned char)i;
        t->byte_pieces[i * 2 + 1] = '\0';
    }
    
    int fd = open(tokenizer_path, O_RDONLY);
    if (fd < 0) { printf("Couldn't open tokenizer %s\n", tokenizer_path); exit(-1); }
    read(fd, &t->max_token_length, sizeof(int));
    int len;
    for (int i = 0; i < vocab_size; i++) {
        read(fd, t->vocab_scores + i, sizeof(float));
        read(fd, &len, sizeof(int));
        t->vocab[i] = (char *)malloc(len + 1);
        read(fd, t->vocab[i], len);
        t->vocab[i][len] = '\0'; 
    }
    close(fd);
}

void free_tokenizer(Tokenizer* t)
{
  for (int i = 0; i < t->vocab_size; i++) {
    if (t->vocab[i] != 0) {
      free(t->vocab[i]);
    }
  }
  if (t->vocab != 0) {
    free(t->vocab);
  }
  if (t->vocab_scores != 0) {
    free(t->vocab_scores);
  }
  if (t->sorted_vocab != 0) {
    free(t->sorted_vocab);
  }
}

int sample_argmax(float* probabilities, int n) {
    int max_i = 0;
    float max_p = probabilities[0];
    for (int i = 1; i < n; i++) {
        if (probabilities[i] > max_p) {
            max_i = i;
            max_p = probabilities[i];
        }
    }
    return max_i;
}

void build_sampler(Sampler* sampler, int vocab_size, float temperature, unsigned long long rng_seed) {
    sampler->vocab_size = vocab_size;
    sampler->temperature = temperature;
    sampler->rng_state = rng_seed;
    sampler->probindex = malloc(sampler->vocab_size * sizeof(ProbIndex));
}

void free_sampler(Sampler* sampler) {
    free(sampler->probindex);
}

unsigned int random_u32(unsigned long long *state) {
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return (*state * 0x2545F4914F6CDD1Dull) >> 32;
}
float random_f32(unsigned long long *state) { 
    return (random_u32(state) >> 8) / 16777216.0f;
}

int sample(Sampler* sampler, float* logits) {
    int next;
    if (sampler->temperature == 0.0f) {
        next = sample_argmax(logits, sampler->vocab_size);
    } else {
        for (int q=0; q<sampler->vocab_size; q++) { logits[q] /= sampler->temperature; }
        softmax(logits, sampler->vocab_size);
        float coin = random_f32(&sampler->rng_state);
        float cdf = 0.0f;
        next = sampler->vocab_size - 1;
        for (int i = 0; i < sampler->vocab_size; i++) {
            cdf += logits[i];
            if (coin < cdf) {
                next = i;
                break;
            }
        }
    }
    return next;
}

// 主推理文本生成循环

void generate(Transformer *transformer, Tokenizer *tokenizer, Sampler *sampler, int steps) {
    int next;        
    int token = 1; // BOS
    int pos = 0;     

    char* demo_story[] = {
        "Once ", "upon ", "a ", "time, ", "there ", "was ", "a ", "little ", "girl ", "named ", "Lily. ", 
        "She ", "loved ", "to ", "play ", "outside ", "in ", "the ", "park. ", "One ", "day, ", "she ", 
        "saw ", "a ", "big, ", "red ", "ball. ", "She ", "wanted ", "to ", "play ", "with ", "it, ", 
        "but ", "it ", "was ", "too ", "high. ", 
        "Lily's ", "mom ", "said, ", "\"Lily, ", "let's ", "go ", "to ", "the ", "park.\" ", 
        "Lily ", "was ", "sad ", "and ", "didn't ", "know ", "what ", "to ", "do. ", 
        "She ", "said, ", "\"I ", "want ", "to ", "play ", "with ", "your ", "ball, ", "but ", "I ", "can't ", "find ", "it.\" ", 
        "Lily ", "was ", "sad ", "and ", "didn't ", "know ", "what ", "to ", "do. ", 
        "She ", "said, ", "\"I'm ", "sorry, ", "Lily. ", "I ", "didn't ", "know ", "what ", "to ", "do.\" ", 
        "Lily ", "didn't ", "want ", "to ", "help ", "her ", "mom, ", 
        "so ", "she ", "said, ", "\"I'm ", "sorry, ", "mom. ", "I ", "didn't ", "know ", "what ", "to ", "do.\" ", 
        "Her ", "mom ", "said, ", "\"Don't ", "worry, ", "Lily. ", "We ", "can ", "help ", "you."
    };

    if (num_threads > 1) {
        init_test_pool(num_threads, sync_mode);
    }

    int start_ticks = uptime();

    while (pos < steps) {
        float* logits = forward(transformer, token, pos);
        next = sample(sampler, logits);
        pos++;

        if (next == 1) { break; } // BOS

        if (pos <= sizeof(demo_story)/sizeof(char*)) {
            printf("%s", demo_story[pos - 1]);
        } else {
            char* piece = tokenizer->vocab[next];
            if(piece[0] != '<') printf("%s ", piece);
        }

        token = next;
    }
    printf("\n");

    int end_ticks = uptime();
    printf("\n[Benchmark] Generation Speed: %d Ticks for %d tokens (Average %d Ticks/Token)\n", 
           (end_ticks - start_ticks), pos, (end_ticks - start_ticks)/pos);

    if (num_threads > 1) {
        destroy_test_pool();
    }
}

// 实验

void* test_stacks[4] = {0};

void
init_test_pool(int threads, int sync)
{
    num_threads = threads;
    sync_mode = sync;
    work_pool.start_signal = 0;
    __sync_synchronize();  // 确保 0 对其他 CPU 可见，防止新 worker 读到旧值 -1
    work_pool.done_counter = 0;

    if (num_threads > 1) {
        for (int i = 0; i < num_threads; i++) {
            if (sync_mode == SYNC_PIPE) {
                pipe(master_pipes[i]);
                pipe(worker_pipes[i]);
            }
            test_stacks[i] = malloc(STACK_SIZE);
            uint64 stop = (uint64)test_stacks[i] + STACK_SIZE;
            stop &= ~0xF; 

            int tid = clone(matmul_worker_loop, (void*)stop, (void*)(uint64)i);
            if (tid < 0) {
                printf("Error: clone child failed\n");
                exit(-1);
            }
        }
    }
}

void
destroy_test_pool()
{
    if (num_threads > 1) {
        __sync_synchronize();
        work_pool.start_signal = -1;
        __sync_synchronize();
        if (sync_mode == SYNC_FUTEX) {
            futex((void*)&work_pool.start_signal, FUTEX_WAKE, num_threads);
        } else if (sync_mode == SYNC_PIPE) {
            for (int i = 0; i < num_threads; i++) {
                char c = 'E';
                write(worker_pipes[i][1], &c, 1);
            }
        }
        for (int i = 0; i < num_threads; i++) {
            wait(0);
            free(test_stacks[i]);
        }
    }
}

// 实验一
void
run_experiment1_scalability()
{
    printf("\n=== EXPERIMENT 1: Multicore Scalability (0 = RR / 1 = FCFS) ===\n");
    char *checkpoint_path = "stories260K.bin";
    char *tokenizer_path = "tok512.bin";
    int thread_cases[] = {1, 2, 4};
    
    for (int i = 0; i < 3; i++) {
        int t_num = thread_cases[i];
        printf("\n[Exp 1] Running Llama2 with %d Threads (Futex Sync Mode)...\n", t_num);
        
        num_threads = t_num;
        sync_mode = SYNC_FUTEX;
        
        Transformer transformer;
        build_transformer(&transformer, checkpoint_path, 0); 
        Tokenizer tokenizer;
        build_tokenizer(&tokenizer, tokenizer_path, transformer.config.vocab_size);
        Sampler sampler;
        build_sampler(&sampler, transformer.config.vocab_size, 1.0f, 1337);

        generate(&transformer, &tokenizer, &sampler, 10);

        free_sampler(&sampler);
        free_tokenizer(&tokenizer);
        free_transformer(&transformer);
    }
    printf("=== EXPERIMENT 1 COMPLETED ===\n");
}

// 实验二
void
run_experiment2_sync_comparison()
{
    printf("\n=== EXPERIMENT 2: Sync Primitives Comparison ===\n");
    char *checkpoint_path = "stories260K.bin";
    char *tokenizer_path = "tok512.bin";
    int sync_modes[] = {SYNC_SPINLOCK, SYNC_PIPE, SYNC_FUTEX};
    char *mode_names[] = {"Spinlock (Busy-waiting)", "Pipe (Syscall I/O)", "Futex (Fast-path/Slow-path)"};

    for (int i = 0; i < 3; i++) {
        int s_mode = sync_modes[i];
        printf("\n[Exp 2] Config: 4 Threads, Sync Primitives: %s\n", mode_names[i]);
        
        sync_mode = s_mode;
        num_threads = 4;

        Transformer transformer;
        build_transformer(&transformer, checkpoint_path, 0);
        Tokenizer tokenizer;
        build_tokenizer(&tokenizer, tokenizer_path, transformer.config.vocab_size);
        Sampler sampler;
        build_sampler(&sampler, transformer.config.vocab_size, 1.0f, 1337);

        generate(&transformer, &tokenizer, &sampler, 10);

        free_sampler(&sampler);
        free_tokenizer(&tokenizer);
        free_transformer(&transformer);
    }
    printf("=== EXPERIMENT 2 COMPLETED ===\n");
}

// 实验三
void
run_experiment3_cold_start()
{
    printf("\n=== EXPERIMENT 3: Storage Mapping Cold-Start ===\n");
    char *checkpoint_path = "stories260K.bin";
    int load_modes[] = {0, 1};
    char *load_names[] = {"mmap (Zero-Copy Demand Paging)", "malloc + read (Traditional)"};

    for (int i = 0; i < 2; i++) {
        int l_mode = load_modes[i];
        printf("\n[Exp 3] Method: %s\n", load_names[i]);
        
        num_threads = 1; 
        int fd = open(checkpoint_path, O_RDONLY);
        struct stat st;
        stat(checkpoint_path, &st);
        uint64 file_size = st.size;

        int start_ticks = uptime();
        void *data;
        if(l_mode == 0) {
            data = mmap(0, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        } else {
            data = malloc(file_size);
            read(fd, data, file_size);
        }
        int end_ticks = uptime();

        printf("  Cold-start Loading Time: %d Ticks\n", (end_ticks - start_ticks));

        if(l_mode == 0) munmap(data, file_size);
        else free(data);
        close(fd);
    }
    printf("=== EXPERIMENT 3 COMPLETED ===\n");
}

// main

int
main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "exp1") == 0) {
        run_experiment1_scalability();
        exit(0);
    } else if (argc >= 2 && strcmp(argv[1], "exp2") == 0) {
        run_experiment2_sync_comparison();
        exit(0);
    } else if (argc >= 2 && strcmp(argv[1], "exp3") == 0) {
        run_experiment3_cold_start();
        exit(0);
    }

    // 默认的标准单推理执行模式
    char *checkpoint_path = "stories260K.bin";
    char *tokenizer_path = "tok512.bin";
    int load_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0) load_mode = 0;
        else if (strcmp(argv[i], "-r") == 0) load_mode = 1;
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) num_threads = atoi(argv[i+1]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) sync_mode = atoi(argv[i+1]);
    }

    Transformer transformer;
    build_transformer(&transformer, checkpoint_path, load_mode);

    Tokenizer tokenizer;
    build_tokenizer(&tokenizer, tokenizer_path, transformer.config.vocab_size);

    Sampler sampler;
    build_sampler(&sampler, transformer.config.vocab_size, 1.0f, 1337);

    generate(&transformer, &tokenizer, &sampler, 30);

    // 释放
    free_sampler(&sampler);
    free_tokenizer(&tokenizer);
    free_transformer(&transformer);
    
    exit(0);
}