#ifndef _BCC_RUNTIME_H_
#   define _BCC_RUNTIME_H_

#ifdef __cplusplus
extern "C" {
#endif

void* FindRuntimeFunction(const char* Name);
void VerifyRuntimesTable();

#ifdef __cplusplus
};
#endif

#endif  /* _BCC_RUNTIME_H_ */
