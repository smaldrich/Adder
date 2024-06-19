#define OPTION_NAMED(okT, errorT, name) \
    typedef struct {                   \
        okT ok;                        \
        errorT error;                  \
    } name;

#define OPTION(okT, errorT) OPTION_NAMED(okT, errorT, okT##Opt)