#define __output(...) printf(__VA_ARGS__)
#define __ERR_format(__fmt__) "ERROR: %s(%d)-<%s>: " __fmt__ "\n"
#define __INFO_format(__fmt__) "INFO: %s(%d)-<%s>: " __fmt__ "\n"

#define LOG_INFO(__fmt__, ...) __output(__INFO_format(__fmt__), __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define LOG_ERROR(__fmt__, ...) __output(__ERR_format(__fmt__), __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)