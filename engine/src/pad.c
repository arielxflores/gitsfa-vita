// Padding at the end of the code segment so vita-elf-create has room for its module data
// (build.sh picks the size after linking).
#ifndef GITSFA_PAD
#define GITSFA_PAD 16
#endif
__attribute__((used)) const char gitsfa_pad[GITSFA_PAD] = { 1 };
