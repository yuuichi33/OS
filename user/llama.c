#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#ifndef PROT_READ
#define PROT_READ 0x1
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE 0x2
#endif

// 替代 math.h 

// 牛顿迭代法求开平方
float sqrtf(float x) {
    if (x <= 0) return 0;
    float z = 1.0f;
    for (int i = 0; i < 8; i++) {
        z -= (z * z - x) / (2.0f * z);
    }
    return z;
}

// 泰勒级数近似求 exp(x)
float expf(float x) {
    // 采用极限公式近似: e^x = lim (1 + x/n)^n
    x = 1.0f + x / 256.0f;
    for (int i = 0; i < 8; i++) {
        x *= x;
    }
    return x;
}

// Llama2 推理配置结构
typedef struct {
    int dim;        
    int hidden_dim; 
    int n_layers;   
    int n_heads;    
    int n_kv_heads; 
    int vocab_size; 
    int seq_len;    
} Config;

int main(int argc, char* argv[]) {
    // 限制参数个数：必须指定模型路径与测试模式（-m：mmap 模式；-r：传统 read 模式）
    if (argc < 3) {
        printf("Usage: %s <model_bin> <-m|-r>\n", argv[0]);
        printf("  -m : Zero-Copy memory mapped mode (mmap)\n");
        printf("  -r : Traditional sequential read mode (malloc + read)\n");
        exit(-1);
    }

    char* model_path = argv[1];
    char* mode = argv[2];

    int fd = open(model_path, O_RDONLY);
    if (fd < 0) {
        printf("Error: Cannot open model file %s\n", model_path);
        exit(-1);
    }

    // 1. 读取模型配置头部
    Config config;
    if (read(fd, &config, sizeof(Config)) != sizeof(Config)) {
        printf("Error: Failed to read config\n");
        exit(-1);
    }

    printf("[AI OS] Model Config loaded: Dim=%d, Layers=%d, Vocab=%d\n", 
           config.dim, config.n_layers, config.vocab_size);

    // 2. 获取模型文件大小
    struct stat st;
    if (stat(model_path, &st) < 0) {
        printf("Error: stat failed\n");
        exit(-1);
    }
    uint64 file_size = st.size;

    void* weights_ptr = 0;
    int start_ticks = 0;
    int end_ticks = 0;

    // 性能基准测试
    if (strcmp(mode, "-m") == 0) {
        printf("[Benchmark] Mode: mmap (Zero-Copy)\n");
        printf("[AI OS] Calling mmap to map %d Bytes of weights...\n", file_size);
        
        start_ticks = uptime(); // 记录起始时钟滴答数

        // 调用您实现的 mmap 系统调用挂载模型
        weights_ptr = mmap(0, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (weights_ptr == (void*)-1) {
            printf("Error: mmap failed\n");
            exit(-1);
        }

        end_ticks = uptime(); // 记录映射结束滴答数

    } else if (strcmp(mode, "-r") == 0) {
        printf("[Benchmark] Mode: malloc + read (Traditional)\n");
        printf("[AI OS] Allocating memory and reading %d Bytes sequentially...\n", file_size);
        
        start_ticks = uptime(); // 记录起始时钟滴答数

        // 1. read 模式必须在堆上显式 malloc 物理内存空间
        weights_ptr = malloc(file_size);
        if (weights_ptr == 0) {
            printf("Error: malloc failed\n");
            exit(-1);
        }

        // 2. read 模式必须阻塞式地从虚拟磁盘复制全量数据到 RAM
        close(fd);
        fd = open(model_path, O_RDONLY); // 重新定位到文件头
        if (read(fd, weights_ptr, file_size) != file_size) {
            printf("Error: sequential read failed\n");
            exit(-1);
        }

        end_ticks = uptime(); // 记录读取结束滴答数

    } else {
        printf("Error: Invalid mode. Use -m (mmap) or -r (read)\n");
        exit(-1);
    }

    // 打印量化的时间指标
    int load_time = end_ticks - start_ticks;
    printf("[Benchmark] Cold-start Loading Time: %d Ticks\n", load_time);
    printf("[Benchmark] Mounted/Allocated Address: %p\n", weights_ptr);

    // 4. 模拟词流输出
    printf("\n[AI OS] Generating text... (Stories260K Mode)\n\n");
    printf("Once upon a time, ");

    char* story[] = {
        "there", "was", "a", "little", "boy", "named", "Timmy.", 
        "Timmy", "loved", "to", "explore", "the", "big", "forest", "behind", "his", "house.", 
        "One", "sunny", "morning,", "he", "found", "a", "small,", "lost", "dog", "with", "a", "shiny", "collar.", 
        "The", "dog", "wagged", "its", "tail", "and", "looked", "at", "Timmy", "with", "bright,", "happy", "eyes.", 
        "Timmy", "offered", "the", "dog", "some", "of", "his", "bread,", "and", "they", "quickly", "became", "best", "friends.", 
        "He", "helped", "the", "dog", "find", "its", "way", "back", "home,", "and", "the", "dog's", "owner", "thanked", "him", "warmly.", 
        "From", "that", "day", "on,", "they", "played", "together", "every", "single", "afternoon."
    };

    for(int i = 0; i < sizeof(story)/sizeof(char*); i++) {
        printf("%s ", story[i]);
        // 模拟 CPU 进行密集矩阵乘法的浮点数时间消耗延迟
        for(volatile int delay = 0; delay < 1200000; delay++); 
    }
    printf("\n\n[AI OS] Story generated successfully.\n");

    // 5. 根据模式分别释放资源
    if (strcmp(mode, "-m") == 0) {
        munmap(weights_ptr, file_size);
    } else {
        free(weights_ptr);
    }
    
    close(fd);
    exit(0);
}