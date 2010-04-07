#ifndef _BCC_RUNTIME_H_
#   define _BCC_RUNTIME_H_

#ifdef __cplusplus
extern "C" {
#endif

    void ListRuntimeFunction();
void* FindRuntimeFunction(const char* Name);

#ifdef __cplusplus
};
#endif

#endif  /* _BCC_RUNTIME_H_ */
