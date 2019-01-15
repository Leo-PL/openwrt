#include <sys/types.h>
int b64_ntop(u_char const* src, size_t srclength, char *target, size_t targsize);
int b64_pton(char const* src, u_char *target, size_t targsize);
