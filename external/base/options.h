#define OPTION_NAME(okT, errorT, name) \
    typedef struct {                   \
        okT ok;                        \
        errorT error;                  \
    } name;

#define OPTION(okT, errorT) OPTION_NAME(okT, errorT, okT##Opt)
