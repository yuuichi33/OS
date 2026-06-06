#include "kernel/types.h"
#include "user/user.h"

void test_illegal_instruction() {
  printf("Triggering: Illegal Instruction...\n");
  // 使用内联汇编插入一条 RISC-V 的非法指令（0x00000000 是无效指令）
  asm volatile(".word 0x00000000");
}

void test_invalid_read() {
  printf("Triggering: Invalid Read (Segmentation Fault)...\n");
  // 访问一个极大的、绝对未映射的虚拟地址（Sv39 架构下的高地址空洞）
  volatile int *p = (int*)0x7FFFFFFFFF; 
  int val = *p;
  printf("Read value: %d (This should not print)\n", val);
}

void test_invalid_write() {
  printf("Triggering: Invalid Write (Segmentation Fault)...\n");
  volatile int *p = (int*)0x7FFFFFFFFF;
  *p = 123;
}

void test_write_to_code() {
  printf("Triggering: Write to Read-Only Code Segment...\n");
  // 地址 0x20 是代码段内部（只读），尝试修改它
  volatile int *p = (int*)0x20;
  *p = 999;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: crash_test <option>\n");
    printf("Options:\n");
    printf("  1: Illegal Instruction (scause 2)\n");
    printf("  2: Invalid Read (scause 13)\n");
    printf("  3: Invalid Write (scause 15)\n");
    printf("  4: Write to Code Segment (scause 15)\n");
    exit(1);
  }

  int option = atoi(argv[1]);
  switch (option) {
    case 1: test_illegal_instruction(); break;
    case 2: test_invalid_read(); break;
    case 3: test_invalid_write(); break;
    case 4: test_write_to_code(); break;
    default: printf("Unknown option: %d\n", option); break;
  }

  printf("Error: Process did not crash as expected.\n");
  exit(0);
}