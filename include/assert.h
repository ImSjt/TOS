#ifndef __LIBS_ASSERT_H__
#define __LIBS_ASSERT_H__

#define ASSERT
#ifdef ASSERT
void assertion_failure(char *exp, char *file, char *base_file, int line);
#define assert(exp) if (exp) ; \
                    else assertion_failure(#exp, __FILE__, __BASE_FILE__, __LINE__)
#else
#define assert(exp)
#endif

void spin(char* funcName);
void panic(const char *fmt, ...);

#endif /* __LIBS_ASSERT_H__ */