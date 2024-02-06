/*******************************************************************************
* libcfgcli.c: this file is part of the libcfgcli library.

* libcfgcli: C library for parsing command line option and configuration files.

* Gitlab repository:
        https://framagit.org/groolot-association/libcfgcli

* Copyright (c) 2019 Cheng Zhao <zhaocheng03@gmail.com>
* Copyright (c) 2023 Gregory David <dev@groolot.net>

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.

*******************************************************************************/

#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "libcfgcli.h"

/*============================================================================*\
                             Definitions of macros
\*============================================================================*/

/* Settings on string allocation. */
#define CFGCLI_STR_INIT_SIZE       1024      /* initial size of dynamic string */
#define CFGCLI_STR_MAX_DOUBLE_SIZE 134217728   /* maximum string doubling size */
#define CFGCLI_NUM_MAX_SIZE(type)  (CHAR_BIT * sizeof(type) / 3 + 2)

/* Settings on the source of the configurations. */
#define CFGCLI_SRC_NULL            0
#define CFGCLI_SRC_OF_OPT(x)       (-x)    /* -x for source being command line */
#define CFGCLI_SRC_VAL(x)          ((x < 0) ? -(x) : x)              /* abs(x) */

/* Definitions of error codes. */
#define CFGCLI_ERR_INIT            (-1)
#define CFGCLI_ERR_MEMORY          (-2)
#define CFGCLI_ERR_INPUT           (-3)
#define CFGCLI_ERR_EXIST           (-4)
#define CFGCLI_ERR_VALUE           (-5)
#define CFGCLI_ERR_PARSE           (-6)
#define CFGCLI_ERR_DTYPE           (-7)
#define CFGCLI_ERR_CMD             (-8)
#define CFGCLI_ERR_FILE            (-9)
#define CFGCLI_ERR_UNKNOWN         (-99)

#define CFGCLI_ERRNO(cfg)          (((cfgcli_error_t *)cfg->error)->errno)
#define CFGCLI_IS_ERROR(cfg)       (CFGCLI_ERRNO(cfg) != 0)

/* Check if a string is a valid command line option, or parser termination. */
#define CFGCLI_IS_OPT(a) (                                                 \
  a[0] == CFGCLI_CMD_FLAG && a[1] &&                                       \
  ((isalpha(a[1]) && (!a[2] || a[2] == CFGCLI_CMD_ASSIGN)) ||              \
  (a[1] == CFGCLI_CMD_FLAG && (!a[2] ||                                    \
  (a[2] != CFGCLI_CMD_ASSIGN && isgraph(a[2])))))    \
  )


/*============================================================================*\
                            Internal data structures
\*============================================================================*/

/* Data structure for storing verified configuration parameters. */
typedef struct {
  cfgcli_dtype_t dtype;            /* data type of the parameter               */
  int src;                      /* source of the value                      */
  int opt;                      /* short command line option                */
  int narr;                     /* number of elements for the array         */
  size_t nlen;                  /* length of the parameter name             */
  size_t llen;                  /* length of the long option                */
  size_t vlen;                  /* length of the value                      */
  size_t hlen;                  /* length of the help message               */
  char *name;                   /* name of the parameter                    */
  char *lopt;                   /* long command line option                 */
  char *value;                  /* value of the parameter                   */
  void *var;                    /* variable for saving the retrieved value  */
  char *help;                   /* parameter help message                   */
} cfgcli_param_valid_t;

/* Data structure for storing verified command line functions. */
typedef struct {
  int called;                   /* 1 if the function is already called      */
  int opt;                      /* short command line option                */
  size_t llen;                  /* length of the long option                */
  size_t hlen;                  /* length of the help message               */
  char *lopt;                   /* long command line option                 */
  void (*func) (void *);        /* pointer to the function                  */
  void *args;                   /* pointer to the arguments                 */
  char *help;                   /* parameter help message                   */
} cfgcli_func_valid_t;

/* Data structure for storing warning/error messages. */
typedef struct {
  int errno;                    /* identifier of the warning/error          */
  int num;                      /* number of existing messages              */
  char *msg;                    /* warning and error messages               */
  size_t len;                   /* length of the existing messages          */
  size_t max;                   /* allocated space for the messages         */
} cfgcli_error_t;

/* Data structure for storing an help line content. */
typedef struct {
  cfgcli_dtype_t dtype;            /* data type of the parameter               */
  int opt;                      /* short command line option                */
  char *lopt;                   /* long command line option                 */
  char *name;                   /* name of the parameter                    */
  char *help;                   /* parameter help message                   */
} cfgcli_help_line_t;

/* String parser states. */
typedef enum {
  CFGCLI_PARSE_START,              CFGCLI_PARSE_KEYWORD,      CFGCLI_PARSE_EQUAL,
  CFGCLI_PARSE_VALUE_START,        CFGCLI_PARSE_VALUE,
  CFGCLI_PARSE_QUOTE,              CFGCLI_PARSE_QUOTE_END,
  CFGCLI_PARSE_ARRAY_START,        CFGCLI_PARSE_ARRAY_VALUE,
  CFGCLI_PARSE_ARRAY_QUOTE,        CFGCLI_PARSE_ARRAY_QUOTE_END,
  CFGCLI_PARSE_ARRAY_NEWLINE,      CFGCLI_PARSE_CLEAN,
  CFGCLI_PARSE_ARRAY_END,          CFGCLI_PARSE_ARRAY_DONE
} cfgcli_parse_state_t;

/* Return value for the parser status. */
typedef enum {
  CFGCLI_PARSE_DONE,
  CFGCLI_PARSE_PASS,
  CFGCLI_PARSE_CONTINUE,
  CFGCLI_PARSE_ERROR
} cfgcli_parse_return_t;


/*============================================================================*\
                       Functions for string manipulation
\*============================================================================*/

/******************************************************************************
Function `cfgcli_strnlen`:
  Compute the length of a string by checking a limited number of characters.
Arguments:
  * `src`:      the input string;
  * `max`:      the maximum number of characters to be checked.
Return:
  The length of the string, including the first '\0'; 0 if '\0' is not found.
******************************************************************************/
static inline size_t cfgcli_strnlen(const char *src, const size_t max) {
  for (size_t i = 0; i < max; i++) if (src[i] == '\0') return i + 1;
  return 0;
}

/******************************************************************************
Function `cfgcli_msg`:
  Append warning/error message to the error handler.
Arguments:
  * `cfg`:      entry for the configurations;
  * `msg`:      the null terminated warning/error message;
  * `key`:      the null terminated keyword for this message.
******************************************************************************/
static void cfgcli_msg(cfgcli_t *cfg, const char *msg, const char *key) {
  if (!msg || *msg == '\0') return;

  cfgcli_error_t *err = (cfgcli_error_t *) cfg->error;
  const size_t msglen = strlen(msg) + 1;        /* suppose msg ends with '\0' */
  size_t keylen, len;
  char *tmp;

  if (!key || *key == '\0') {
    keylen = 0;
    len = msglen;                               /* append only "msg" */
  }
  else {
    keylen = strlen(key) + 1;
    len = msglen + keylen + 1;                  /* append "msg: key" */
  }

  /* Double the allocated size if the space is not enough. */
  len += err->len;
  if (len > err->max) {
    size_t max = 0;
    if (err->max == 0) max = len;
    else if (err->max >= CFGCLI_STR_MAX_DOUBLE_SIZE) {
      if (SIZE_MAX - CFGCLI_STR_MAX_DOUBLE_SIZE >= err->max)
        max = CFGCLI_STR_MAX_DOUBLE_SIZE + err->max;
    }
    else if (SIZE_MAX / 2 >= err->max) max = err->max << 1;
    if (!max) {
      err->errno = CFGCLI_ERR_MEMORY;
      return;
    }
    if (len > max) max = len;           /* the size is still not enough */

    tmp = realloc(err->msg, max);
    if (!tmp) {
      err->errno = CFGCLI_ERR_MEMORY;
      return;
    }
    err->msg = tmp;
    err->max = max;
  }

  /* Record the message. */
  tmp = err->msg + err->len;
  memcpy(tmp, msg, msglen);                     /* '\0' is copied */
  if (keylen) {
    *(tmp + msglen - 1) = ':';
    *(tmp + msglen) = ' ';
    memcpy(tmp + msglen + 1, key, keylen);
  }
  err->len = len;
  err->num += 1;
}


/*============================================================================*\
              Functions for initialising parameters and functions
\*============================================================================*/

/******************************************************************************
Function `cfgcli_init`:
  Initialise the entry for all parameters and command line functions.
Return:
  The address of the structure.
******************************************************************************/
cfgcli_t *cfgcli_init(void) {
  cfgcli_t *cfg = calloc(1, sizeof(cfgcli_t));
  if (!cfg) return NULL;

  cfgcli_error_t *err = calloc(1, sizeof(cfgcli_error_t));
  if (!err) {
    free(cfg);
    return NULL;
  }
  err->msg = NULL;

  cfg->params = cfg->funcs = NULL;
  cfg->error = err;
  return cfg;
}

/******************************************************************************
Function `cfgcli_print_help_line`:
  Print short and long options with their dashes
Arguments:
  * `buffer`:  output buffer with options sets
  * `help_line`:  content to use for display
Return:
  Output string length.
******************************************************************************/
static size_t cfgcli_print_help_line(char *buffer, const cfgcli_help_line_t *help_line) {
  size_t len = 0;
  if (help_line->opt) {
    len += sprintf(buffer, " -%c", help_line->opt);
    if (help_line->lopt) {
      len += sprintf(buffer + len, ",");
    }
  }
  if (help_line->lopt) {
    len += sprintf(buffer + len, " --%s", help_line->lopt);
  }
  if (help_line->dtype != CFGCLI_DTYPE_NULL && help_line->dtype != CFGCLI_DTYPE_BOOL) {
    len += sprintf(buffer + len, " %s", help_line->name);
  }
  if (help_line->help) {
    len += sprintf(buffer + len, "\n    %s", help_line->help);
  }
  return len;
}

/******************************************************************************
Function `cfgcli_print_param`:
  Wrapper to `cfgcli_print_help_line` for `cfgcli_param_valid_t` parameter
Arguments:
  * `buffer`:  output buffer with options sets
  * `param`:  parameter to display
Return:
  Output string length.
******************************************************************************/
static size_t cfgcli_print_param(char *buffer, const cfgcli_param_valid_t *param) {
  cfgcli_help_line_t *help_content = malloc(sizeof(cfgcli_help_line_t));
  help_content->dtype = param->dtype;
  help_content->opt = param->opt;
  help_content->lopt = param->lopt;
  help_content->name = param->name;
  help_content->help = param->help;
  return cfgcli_print_help_line(buffer, help_content);
}

/******************************************************************************
Function `cfgcli_print_func`:
  Wrapper to `cfgcli_print_help_line` for `cfgcli_func_valid_t` parameter
Arguments:
  * `buffer`:  output buffer with options sets
  * `func`:  function to display
Return:
  Output string length.
******************************************************************************/
static size_t cfgcli_print_func(char *buffer, const cfgcli_func_valid_t *func) {
  cfgcli_help_line_t *help_content = malloc(sizeof(cfgcli_help_line_t));
  help_content->dtype = CFGCLI_DTYPE_NULL;
  help_content->opt = func->opt;
  help_content->lopt = func->lopt;
  help_content->name = NULL;
  help_content->help = func->help;
  return cfgcli_print_help_line(buffer, help_content);
}

/******************************************************************************
Function `cfgcli_print_help`:
  Print help messages based on validated parameters
Arguments:
  * `cfg`:      entry for all configuration parameters;
******************************************************************************/
void cfgcli_print_help(cfgcli_t *cfg) {
	size_t used_len = 0;
	int dash_size_overload = 13;
	char buffer[CFGCLI_MAX_LOPT_LEN + CFGCLI_MAX_NAME_LEN + dash_size_overload + CFGCLI_MAX_HELP_LEN];
	bzero(buffer, CFGCLI_MAX_LOPT_LEN + CFGCLI_MAX_NAME_LEN + dash_size_overload + CFGCLI_MAX_HELP_LEN);
	if (cfg && !CFGCLI_IS_ERROR(cfg)) {
		if (cfg->npar > 0) {
			const cfgcli_param_valid_t *param = (cfgcli_param_valid_t *)cfg->params;
			printf("Option%c:\n", cfg->npar > 1 ? 's' : '\0');
			for (size_t i = 0; i < cfg->npar; ++i) {
				used_len = cfgcli_print_param(buffer, &param[i]);
				printf("%s\n", buffer);
				bzero(buffer, used_len);
			}
			printf("\n");
		}
		else {
			cfgcli_msg(cfg, "the parameter list is not set", NULL);
		}
		if (cfg->nfunc > 0) {
			const cfgcli_func_valid_t *param = (cfgcli_func_valid_t *)cfg->funcs;
			printf("Function%c:\n", cfg->nfunc > 1 ? 's' : '\0');
			for (size_t i = 0; i < cfg->nfunc; ++i) {
				used_len = cfgcli_print_func(buffer, &param[i]);
				printf("%s\n", buffer);
				bzero(buffer, used_len);
			}
			printf("\n");
		}
		else {
			cfgcli_msg(cfg, "the function list is not set", NULL);
		}
	}
}

/******************************************************************************
Function `cfgcli_print_usage`:
  Print usage messages based on validated parameters and provided progname
Arguments:
  * `cfg`:      entry for all configuration parameters;
  * `progname`: program name to be displayed
******************************************************************************/
void cfgcli_print_usage(cfgcli_t *cfg, char *progname) {
  if (cfg && !CFGCLI_IS_ERROR(cfg)) {
    char options[] = " [OPTIONS]";
    char functions[] = " [FUNCTIONS]";
    const char local_progname[] = "program";
    if (!progname || *progname == '\0') {
      progname = (char *)local_progname;
    }
    if (cfg->npar == 1) {
      options[8] = ']'; options[9] = '\0'; }  // Singularize
    else if (cfg->npar == 0)
      *options = '\0'; // Remove options
    if (cfg->nfunc == 1) {
      functions[10] = ']';
      functions[11] = '\0';
    }  // Singularize
    else if (cfg->nfunc == 0)
      *functions = '\0'; // Remove functions
    printf("Usage: %s%s%s\n", progname, options, functions);
  }
}

/******************************************************************************
Function `cfgcli_set_params`:
  Verify and register configuration parameters.
Arguments:
  * `cfg`:      entry for all configuration parameters;
  * `param`:    stucture for the input configuration parameters;
  * `npar`:     number of input configuration parameters.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int cfgcli_set_params(cfgcli_t *cfg, const cfgcli_param_t *param, const int npar) {
  /* Validate arguments. */
  if (!cfg) return CFGCLI_ERR_INIT;
  if (CFGCLI_IS_ERROR(cfg)) return CFGCLI_ERRNO(cfg);
  if (!param || npar <= 0) {
    cfgcli_msg(cfg, "the parameter list is not set", NULL);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
  }

  /* Allocate memory for parameters. */
  cfgcli_param_valid_t *vpar = realloc(cfg->params,
      (npar + cfg->npar) * sizeof *vpar);
  if (!vpar) {
    cfgcli_msg(cfg, "failed to allocate memory for parameters", NULL);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_MEMORY;
  }
  memset(vpar + cfg->npar, 0, npar * sizeof *vpar);
  cfg->params = vpar;

  /* Register parameters. */
  for (int i = 0; i < npar; i++) {
    /* Reset the parameter holder. */
    cfgcli_param_valid_t *par = (cfgcli_param_valid_t *) cfg->params + cfg->npar + i;
    par->dtype = CFGCLI_DTYPE_NULL;
    par->src = CFGCLI_SRC_NULL;
    par->help = par->name = par->lopt = par->value = NULL;
    par->var = NULL;

    /* Create the string for the current index and short option. */
    char tmp[CFGCLI_NUM_MAX_SIZE(int)];
    sprintf(tmp, "%d", i);

    /* Verify the name. */
    char *str = param[i].name;
    if (!str || (!isalpha(*str) && *str != '_' && *str != '-')) {
      cfgcli_msg(cfg, "invalid parameter name in the list with index", tmp);
      return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
    }
    int j = 1;
    while (str[j] != '\0') {
      if (!isalnum(str[j]) && str[j] != '_' && str[j] != '-') {
        cfgcli_msg(cfg, "invalid parameter name in the list with index", tmp);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
      }
      if (++j >= CFGCLI_MAX_NAME_LEN) {            /* no null termination */
        cfgcli_msg(cfg, "invalid parameter name in the list with index", tmp);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
      }
    }
    par->name = str;
    par->nlen = j + 1;       /* length of name with the ending '\0' */

    /* Verify the data type. */
    if (CFGCLI_DTYPE_INVALID(param[i].dtype)) {
      cfgcli_msg(cfg, "invalid data type for parameter", par->name);
      return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
    }
    par->dtype = param[i].dtype;
    if (!param[i].var) {
      cfgcli_msg(cfg, "variable unset for parameter", par->name);
      return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
    }
    par->var = param[i].var;

    /* Verify command line options. */
    char opt[3] = { 0 };
    if (isalpha(param[i].opt)) {
      par->opt = param[i].opt;
      opt[0] = '-';
      opt[1] = par->opt;
    }
    else if (param[i].opt) {
      cfgcli_msg(cfg, "invalid short command line option for parameter",
          par->name);
    }

    str = param[i].lopt;
    if (str && str[j = 0] != '\0') {
      do {
        if (!isgraph(str[j]) || str[j] == CFGCLI_CMD_ASSIGN) {
          cfgcli_msg(cfg, "invalid long command line option for parameter",
              par->name);
          break;
        }
        if (++j >= CFGCLI_MAX_LOPT_LEN) {          /* no null termination */
          cfgcli_msg(cfg, "invalid long command line option for parameter",
              par->name);
          return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
        }
      }
      while (str[j] != '\0');
      if (str[j] == '\0') {
        par->lopt = str;
        par->llen = j + 1;      /* length of long option with the ending '\0' */
      }
    }

    tmp[0] = par->opt;
    tmp[1] = '\0';

    /* Verify the help message. */
    str = param[i].help;
    j = 0;
    while (str[j] != '\0') {
      if (++j >= CFGCLI_MAX_HELP_LEN) {            /* no null termination */
        cfgcli_msg(cfg, "invalid help (too long) for parameter", par->lopt ? par->lopt : opt);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
      }
    }
    par->help = str;
    par->hlen = j + 1;       /* length of help with the ending '\0' */

    /* Check duplicates with the registered parameters. */
    for (j = 0; j < cfg->npar + i; j++) {
      if (!strncmp(par->name, vpar[j].name, par->nlen)) {
        cfgcli_msg(cfg, "duplicate parameter name", par->name);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
      if (par->opt && par->opt == vpar[j].opt) {
        cfgcli_msg(cfg, "duplicate short command line option", tmp);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
      if (par->lopt && vpar[j].lopt &&
          !strncmp(par->lopt, vpar[j].lopt, par->llen)) {
        cfgcli_msg(cfg, "duplicate long command line option", par->lopt);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
    }

    /* Check duplicates with the registered functions. */
    cfgcli_func_valid_t *cfunc = (cfgcli_func_valid_t *) cfg->funcs;
    for (j = 0; j < cfg->nfunc; j++) {
      if (par->opt && par->opt == cfunc[j].opt) {
        cfgcli_msg(cfg, "duplicate short command line option", tmp);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
      if (par->lopt && cfunc[j].lopt &&
          !strncmp(par->lopt, cfunc[j].lopt, par->llen)) {
        cfgcli_msg(cfg, "duplicate long command line option", par->lopt);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
    }
  }

  cfg->npar += npar;
  return 0;
}

/******************************************************************************
Function `cfgcli_set_funcs`:
  Verify and register command line functions.
Arguments:
  * `cfg`:      entry for all command line functions;
  * `func`:     stucture for the input functions;
  * `nfunc`:    number of input functions.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int cfgcli_set_funcs(cfgcli_t *cfg, const cfgcli_func_t *func, const int nfunc) {
  /* Validate arguments. */
  if (!cfg) return CFGCLI_ERR_INIT;
  if (CFGCLI_IS_ERROR(cfg)) return CFGCLI_ERRNO(cfg);
  if (!func || nfunc <= 0) {
    cfgcli_msg(cfg, "the function list is not set", NULL);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
  }

  /* Allocate memory for command line functions. */
  cfgcli_func_valid_t *vfunc = realloc(cfg->funcs,
      (nfunc + cfg->nfunc) * sizeof *vfunc);
  if (!vfunc) {
    cfgcli_msg(cfg, "failed to allocate memory for functions", NULL);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_MEMORY;
  }
  memset(vfunc + cfg->nfunc, 0, nfunc * sizeof *vfunc);
  cfg->funcs = vfunc;

  /* Register command line functions. */
  for (int i = 0; i < nfunc; i++) {
    /* Reset the command line function holder. */
    cfgcli_func_valid_t *fun = (cfgcli_func_valid_t *) cfg->funcs + cfg->nfunc + i;
    fun->lopt = NULL;
    fun->func = NULL;
    fun->args = NULL;
    fun->help = NULL;

    /* Create the string for the current index and short option. */
    char tmp[CFGCLI_NUM_MAX_SIZE(int)];
    sprintf(tmp, "%d", i);

    /* Verify the command line options. */
    char opt[3] = { 0 };
    if (isalpha(func[i].opt)) {
      fun->opt = func[i].opt;
      opt[0] = '-';
      opt[1] = (char)fun->opt;
    }
    else if (func[i].opt)
      cfgcli_msg(cfg, "invalid short command line option for function index", tmp);

    char *str = func[i].lopt;
    int j = 0;
    if (str && str[j] != '\0') {
      do {
        if (!isgraph(str[j]) || str[j] == CFGCLI_CMD_ASSIGN) {
          cfgcli_msg(cfg, "invalid long command line option for function index",
              tmp);
          break;
        }
        if (++j >= CFGCLI_MAX_LOPT_LEN) {          /* no null termination */
          cfgcli_msg(cfg, "invalid long command line option for function index",
              tmp);
          return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
        }
      }
      while (str[j] != '\0');
      if (str[j] == '\0') {
        fun->lopt = str;
        fun->llen = j + 1;      /* length of long option with the ending '\0' */
      }
    }

    if (!fun->opt && !fun->lopt) {
      cfgcli_msg(cfg, "no valid command line option for function index", tmp);
      return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
    }

    /* Verify the function pointer. */
    if (!(fun->func = func[i].func)) {
      cfgcli_msg(cfg, "function not set with index", tmp);
      return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
    }
    fun->args = func[i].args;

    /* Verify the help message. */
    if (func[i].help) {
      str = func[i].help;
      j = 0;
      while (str[j] != '\0') {
        if (++j >= CFGCLI_MAX_HELP_LEN) {            /* no null termination */
          cfgcli_msg(cfg, "invalid help (too long) for function", fun->lopt ? fun->lopt : opt);
          return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
        }
      }
      fun->help = str;
      fun->hlen = j + 1;       /* length of help with the ending '\0' */
    }

    /* Check duplicates with the registered functions. */
    for (j = 0; j < cfg->nfunc + i; j++) {
      /* Function and arguments cannot both be identical. */
      if (fun->func == vfunc[j].func && fun->args == vfunc[j].args) {
        cfgcli_msg(cfg, "duplicate function with index", tmp);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
      if (fun->opt && fun->opt == vfunc[j].opt) {
        tmp[0] = fun->opt;
        tmp[1] = '\0';
        cfgcli_msg(cfg, "duplicate short command line option", tmp);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
      if (fun->lopt && vfunc[j].lopt &&
          !strncmp(fun->lopt, vfunc[j].lopt, fun->llen)) {
        cfgcli_msg(cfg, "duplicate long command line option", fun->lopt);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
    }

    /* Check duplicates with the registered parameters. */
    cfgcli_param_valid_t *cpar = (cfgcli_param_valid_t *) cfg->params;
    for (j = 0; j < cfg->npar; j++) {
      if (fun->opt && fun->opt == cpar[j].opt) {
        tmp[0] = fun->opt;
        tmp[1] = '\0';
        cfgcli_msg(cfg, "duplicate short command line option", tmp);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
      if (fun->lopt && cpar[j].lopt &&
          !strncmp(fun->lopt, cpar[j].lopt, fun->llen)) {
        cfgcli_msg(cfg, "duplicate long command line option", fun->lopt);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_EXIST;
      }
    }
  }

  cfg->nfunc += nfunc;
  return 0;
}


/*============================================================================*\
          Functions for parsing configurations represented by strings
\*============================================================================*/

/******************************************************************************
Function `cfgcli_parse_line`:
  Read the configuration from a line of a configration file.
Arguments:
  * `line`:     the null terminated string line;
  * `len`:      length of the line, NOT including the first '\0';
  * `key`:      address of the retrieved keyword;
  * `value`:    address of the retrieved value;
  * `state`:    initial state for the parser.
Return:
  Parser status.
******************************************************************************/
static cfgcli_parse_return_t cfgcli_parse_line(char *line, const size_t len,
    char **key, char **value, cfgcli_parse_state_t state) {
  if (!line || *line == '\0' || len == 0) return CFGCLI_PARSE_PASS;
  char quote = '\0';            /* handle quotation marks */
  char *newline = NULL;                /* handle line continuation */
  for (size_t i = 0; i < len; i++) {
    char c = line[i];
    switch (state) {
      case CFGCLI_PARSE_START:
        if (isalpha(c) || c == '_' || c == '-') {
          *key = line + i;
          state = CFGCLI_PARSE_KEYWORD;
        }
        else if (c == CFGCLI_SYM_COMMENT) return CFGCLI_PARSE_PASS;
        else if (!isspace(c)) return CFGCLI_PARSE_ERROR;
        break;
      case CFGCLI_PARSE_KEYWORD:
        if (c == CFGCLI_SYM_EQUAL || isspace(c)) {
          /* check if the keyword is too long */
          if (line + i - *key >= CFGCLI_MAX_NAME_LEN) return CFGCLI_PARSE_ERROR;
          line[i] = '\0';                       /* terminate the keyword */
          state = (c == CFGCLI_SYM_EQUAL) ?
            CFGCLI_PARSE_VALUE_START : CFGCLI_PARSE_EQUAL;
        }
        else if (!isalnum(c) && c != '_' && c != '-') return CFGCLI_PARSE_ERROR;
        break;
      case CFGCLI_PARSE_EQUAL:
        if (c == CFGCLI_SYM_EQUAL) state = CFGCLI_PARSE_VALUE_START;
        else if (!isspace(c)) return CFGCLI_PARSE_ERROR;
        break;
      case CFGCLI_PARSE_VALUE_START:
        if (c == '"' || c == '\'') {            /* enter quotes */
          quote = c;
          *value = line + i;
          state = CFGCLI_PARSE_QUOTE;
        }
        else if (c == CFGCLI_SYM_ARRAY_START) {    /* beginning of array */
          *value = line + i;
          state = CFGCLI_PARSE_ARRAY_START;
        }
        else if (c == CFGCLI_SYM_COMMENT) return CFGCLI_PARSE_PASS;   /* no value */
        else if (isgraph(c)) {                  /* beginning of value */
          *value = line + i;
          state = CFGCLI_PARSE_VALUE;
        }
        else if (!isspace(c)) return CFGCLI_PARSE_ERROR;
        break;
      case CFGCLI_PARSE_ARRAY_START:
        if (c == CFGCLI_SYM_ARRAY_SEP || c == CFGCLI_SYM_ARRAY_END ||
            c == CFGCLI_SYM_COMMENT)               /* no value is found */
          return CFGCLI_PARSE_ERROR;
        else if (c == '"' || c == '\'')  {      /* enter quotes */
          quote = c;
          state = CFGCLI_PARSE_ARRAY_QUOTE;
        }
        else if (c == CFGCLI_SYM_NEWLINE) {        /* line continuation */
          newline = line + i;
          state = CFGCLI_PARSE_ARRAY_NEWLINE;
        }
        else if (isgraph(c)) state = CFGCLI_PARSE_ARRAY_VALUE;
        else if (!isspace(c)) return CFGCLI_PARSE_ERROR;
        break;
      case CFGCLI_PARSE_VALUE:
        if (c == CFGCLI_SYM_COMMENT) {
          line[i] = '\0';                       /* terminate the value */
          return CFGCLI_PARSE_DONE;
        }
        else if (!isprint(c)) return CFGCLI_PARSE_ERROR;
        break;
      case CFGCLI_PARSE_ARRAY_VALUE:
        if (c == CFGCLI_SYM_ARRAY_SEP)             /* new array element */
          state = CFGCLI_PARSE_ARRAY_START;
        else if (c == CFGCLI_SYM_ARRAY_END)        /* end of array */
          state = CFGCLI_PARSE_ARRAY_END;
        else if (c == CFGCLI_SYM_COMMENT || !isprint(c)) return CFGCLI_PARSE_ERROR;
        break;
      case CFGCLI_PARSE_QUOTE:
        if (c == quote) state = CFGCLI_PARSE_QUOTE_END;
        break;
      case CFGCLI_PARSE_ARRAY_QUOTE:
        if (c == quote) state = CFGCLI_PARSE_ARRAY_QUOTE_END;
        break;
      case CFGCLI_PARSE_QUOTE_END:
      case CFGCLI_PARSE_ARRAY_END:
        if (c == CFGCLI_SYM_COMMENT) {
          line[i] = '\0';
          return CFGCLI_PARSE_DONE;
        }
        else if (!isspace(c)) return CFGCLI_PARSE_ERROR;
        break;
      case CFGCLI_PARSE_ARRAY_QUOTE_END:
        if (c == CFGCLI_SYM_ARRAY_SEP) state = CFGCLI_PARSE_ARRAY_START;
        else if (c == CFGCLI_SYM_ARRAY_END) state = CFGCLI_PARSE_ARRAY_END;
        else if (!isspace(c)) return CFGCLI_PARSE_ERROR;
        break;
      case CFGCLI_PARSE_ARRAY_NEWLINE:             /* line continuation */
        if (c == CFGCLI_SYM_COMMENT) {             /* clear all characters */
          state = CFGCLI_PARSE_CLEAN;
          line[i] = ' ';
          *newline = ' ';
        }
        else if (!isspace(c)) {         /* not really for line continuation */
          newline = NULL;
          state = CFGCLI_PARSE_ARRAY_VALUE;
          i--;
        }
        break;
      case CFGCLI_PARSE_CLEAN:
        line[i] = ' ';
        break;
      default:
        return CFGCLI_PARSE_ERROR;
    }
  }

  /* Check the final status. */
  switch (state) {
    case CFGCLI_PARSE_VALUE:
    case CFGCLI_PARSE_QUOTE_END:
    case CFGCLI_PARSE_ARRAY_END:
      return CFGCLI_PARSE_DONE;
    case CFGCLI_PARSE_START:
    case CFGCLI_PARSE_VALUE_START:
      return CFGCLI_PARSE_PASS;
    case CFGCLI_PARSE_ARRAY_NEWLINE:
      *newline = ' ';
#if __STDC_VERSION__ > 201710L
      [[fallthrough]];
#endif
    case CFGCLI_PARSE_CLEAN:
      return CFGCLI_PARSE_CONTINUE;
    default:
      return CFGCLI_PARSE_ERROR;
  }
}

/******************************************************************************
Function `cfgcli_parse_array`:
  Split the string defined as array, and count the number of elements.
Arguments:
  * `par`:      address of the verified configuration parameter.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
static int cfgcli_parse_array(cfgcli_param_valid_t *par) {
  par->narr = 0;                /* no need to check whether `par` is NULL */
  if (!par->value || !par->vlen) return 0;              /* empty string */

  int n = 0;
  char quote = '\0';
  cfgcli_parse_state_t state = CFGCLI_PARSE_START;
  char *start, *end;
  start = end = NULL;

  for (size_t i = 0; i < par->vlen; i++) {
    if (state == CFGCLI_PARSE_ARRAY_DONE) break;
    char c = par->value[i];             /* this is surely not '\0' */

    switch (state) {
      case CFGCLI_PARSE_START:
        if (c == CFGCLI_SYM_ARRAY_START) {
          state = CFGCLI_PARSE_ARRAY_START;
          start = par->value + i;       /* mark the array starting point */
        }
        else if (!isspace(c)) {                 /* not an array */
          par->narr = 1;        /* try to parse as a single variable later */
          return 0;
        }
        break;
      case CFGCLI_PARSE_ARRAY_START:
        if (c == CFGCLI_SYM_ARRAY_SEP || c == CFGCLI_SYM_ARRAY_END ||
            c == CFGCLI_SYM_COMMENT)               /* no value is found */
          return CFGCLI_ERR_VALUE;
        else if (c == '"' || c == '\'')  {      /* enter quotes */
          quote = c;
          state = CFGCLI_PARSE_ARRAY_QUOTE;
        }
        else if (isgraph(c)) state = CFGCLI_PARSE_ARRAY_VALUE;
        else if (!isspace(c)) return CFGCLI_ERR_VALUE;
        break;
      case CFGCLI_PARSE_ARRAY_VALUE:
        if (c == CFGCLI_SYM_ARRAY_SEP) {      /* new array element */
          n++;
          state = CFGCLI_PARSE_ARRAY_START;
          par->value[i] = '\0';         /* add separator for value parser */
        }
        else if (c == CFGCLI_SYM_ARRAY_END) {      /* end of array */
          state = CFGCLI_PARSE_ARRAY_END;
          end = par->value + i;         /* mark the array ending point */
        }
        else if (c == CFGCLI_SYM_COMMENT || !isprint(c)) return CFGCLI_ERR_VALUE;
        break;
      case CFGCLI_PARSE_ARRAY_QUOTE:
        if (c == quote) {
          quote = '\0';
          state = CFGCLI_PARSE_ARRAY_QUOTE_END;
        }
        break;
      case CFGCLI_PARSE_ARRAY_QUOTE_END:
        if (c == CFGCLI_SYM_ARRAY_SEP) {           /* new array element */
          n++;
          state = CFGCLI_PARSE_ARRAY_START;
          par->value[i] = '\0';         /* add separator for value parser */
        }
        else if (c == CFGCLI_SYM_ARRAY_END) {      /* end of array */
          state = CFGCLI_PARSE_ARRAY_END;
          end = par->value + i;         /* mark the array ending point */
        }
        else if (!isspace(c)) return CFGCLI_ERR_VALUE;
        break;
      case CFGCLI_PARSE_ARRAY_END:
        if (c == CFGCLI_SYM_COMMENT) {
          state = CFGCLI_PARSE_ARRAY_DONE;
          par->value[i] = '\0';         /* terminate earlier to skip comments */
        }
        else if (isgraph(c)) return CFGCLI_ERR_VALUE;
        break;
      default:
        return CFGCLI_ERR_VALUE;
    }
  }
  if (start)
      par->value = start + 1;       /* omit the starting '[' */
  if (end)
      *end = '\0';                  /* remove the ending ']' */
  par->narr = n + 1;
  return 0;
}

/******************************************************************************
Function `cfgcli_get_value`:
  Retrieve the parameter value and assign it to a variable.
Arguments:
  * `var`:      pointer to the variable to be assigned value;
  * `str`:      string storing the parameter value;
  * `size`:     length of `value`, including the ending '\0';
  * `dtype`:    data type;
  * `src`:      source of this value.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
static int cfgcli_get_value(void *var, char *str, const size_t size,
                         const cfgcli_dtype_t dtype, int src) {
  (void) src;
  if (!str || !size) return 0;
  char *value = str;
  int n;

  /* Validate the value. */
  while (*value && isspace(*value)) value++;          /* omit whitespaces */
  if (*value == '\0') return CFGCLI_ERR_VALUE;           /* empty string */
  if (*value == '"' || *value == '\'') {              /* remove quotes */
    char quote = *value;
    value++;
    for (n = 0; value[n]; n++) {      /* the string is surely terminated */
      if (value[n] == quote) {
        value[n] = '\0';
        quote = 0;    /* a flag indicating the other quote is found */
        break;
      }
    }
    /* empty string with quotes is valid for char or string type variable */
    if (*value == '\0' && dtype != CFGCLI_DTYPE_CHAR && dtype != CFGCLI_DTYPE_STR)
      return CFGCLI_ERR_VALUE;
    if (quote) return CFGCLI_ERR_VALUE;          /* open quotation marks */
    for (++n; value[n]; n++) if (!isspace(value[n])) return CFGCLI_ERR_VALUE;
  }
  else {                              /* remove trailing whitespaces */
    char *val = str + size - 2;
    while (isspace(*val)) {
      *val = '\0';
      val--;
    }
  }

  /* Variable assignment. */
  n = 0;
  switch (dtype) {
    case CFGCLI_DTYPE_BOOL:
      if (!strncmp(value, "1", 2) || !strncmp(value, "T", 2) ||
          !strncmp(value, "t", 2) || !strncmp(value, "true", 5) ||
          !strncmp(value, "TRUE", 5) || !strncmp(value, "True", 5))
        *((bool *) var) = true;
      else if (!strncmp(value, "0", 2) || !strncmp(value, "F", 2) ||
          !strncmp(value, "f", 2) || !strncmp(value, "false", 6) ||
          !strncmp(value, "FALSE", 6) || !strncmp(value, "False", 6))
        *((bool *) var) = false;
      else return CFGCLI_ERR_PARSE;
      break;
    case CFGCLI_DTYPE_CHAR:
      *((char *) var) = *value;
      n = 1;
      break;
    case CFGCLI_DTYPE_INT:
      if (sscanf(value, "%d%n", (int *) var, &n) != 1) return CFGCLI_ERR_PARSE;
      break;
    case CFGCLI_DTYPE_LONG:
      if (sscanf(value, "%ld%n", (long *) var, &n) != 1) return CFGCLI_ERR_PARSE;
      break;
    case CFGCLI_DTYPE_FLT:
      if (sscanf(value, "%f%n", (float *) var, &n) != 1) return CFGCLI_ERR_PARSE;
      break;
    case CFGCLI_DTYPE_DBL:
      if (sscanf(value, "%lf%n", (double *) var, &n) != 1) return CFGCLI_ERR_PARSE;
      break;
    case CFGCLI_DTYPE_STR:
      strcpy(*((char **) var), value);  /* the usage of strcpy is safe here */
      break;
    default:
      return CFGCLI_ERR_DTYPE;
  }

  if (n) {                      /* check remaining characters */
    value += n;
    while (*value != '\0') {
      if (!isspace(*value)) return CFGCLI_ERR_VALUE;
      value++;
    }
  }

  return 0;
}

/******************************************************************************
Function `cfgcli_get_array`:
  Retrieve the parameter values and assign them to an array.
Arguments:
  * `par`:      address of the verified configuration parameter;
  * `src`:      source of the value.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
static int cfgcli_get_array(cfgcli_param_valid_t *par, int src) {
  size_t len;
  int i, err;

  /* Split the value string for array elements. */
  if ((err = cfgcli_parse_array(par))) return err;
  char *value = par->value;   /* array elements are separated by '\0' */

  /* Allocate memory and assign values for arrays. */
  switch (par->dtype) {
    case CFGCLI_ARRAY_BOOL:
      *((bool **) par->var) = calloc(par->narr, sizeof(bool));
      if (!(*((bool **) par->var))) return CFGCLI_ERR_MEMORY;
      /* call the value assignment function for each segment */
      for (i = 0; i < par->narr; i++) {
        len = strlen(value) + 1;      /* strlen is safe here */
        if ((err = cfgcli_get_value(*((bool **) par->var) + i, value, len,
            CFGCLI_DTYPE_BOOL, src))) return err;
        value += len;
      }
      break;
    case CFGCLI_ARRAY_CHAR:
      *((char **) par->var) = calloc(par->narr, sizeof(char));
      if (!(*((char **) par->var))) return CFGCLI_ERR_MEMORY;
      for (i = 0; i < par->narr; i++) {
        len = strlen(value) + 1;
        if ((err = cfgcli_get_value(*((char **) par->var) + i, value, len,
            CFGCLI_DTYPE_CHAR, src))) return err;
        value += len;
      }
      break;
    case CFGCLI_ARRAY_INT:
      *((int **) par->var) = calloc(par->narr, sizeof(int));
      if (!(*((int **) par->var))) return CFGCLI_ERR_MEMORY;
      for (i = 0; i < par->narr; i++) {
        len = strlen(value) + 1;
        if ((err = cfgcli_get_value(*((int **) par->var) + i, value, len,
            CFGCLI_DTYPE_INT, src))) return err;
        value += len;
      }
      break;
    case CFGCLI_ARRAY_LONG:
      *((long **) par->var) = calloc(par->narr, sizeof(long));
      if (!(*((long **) par->var))) return CFGCLI_ERR_MEMORY;
      for (i = 0; i < par->narr; i++) {
        len = strlen(value) + 1;
        if ((err = cfgcli_get_value(*((long **) par->var) + i, value, len,
            CFGCLI_DTYPE_LONG, src))) return err;
        value += len;
      }
      break;
    case CFGCLI_ARRAY_FLT:
      *((float **) par->var) = calloc(par->narr, sizeof(float));
      if (!(*((float **) par->var))) return CFGCLI_ERR_MEMORY;
      for (i = 0; i < par->narr; i++) {
        len = strlen(value) + 1;
        if ((err = cfgcli_get_value(*((float **) par->var) + i, value, len,
            CFGCLI_DTYPE_FLT, src))) return err;
        value += len;
      }
      break;
    case CFGCLI_ARRAY_DBL:
      *((double **) par->var) = calloc(par->narr, sizeof(double));
      if (!(*((double **) par->var))) return CFGCLI_ERR_MEMORY;
      for (i = 0; i < par->narr; i++) {
        len = strlen(value) + 1;
        if ((err = cfgcli_get_value(*((double **) par->var) + i, value, len,
            CFGCLI_DTYPE_DBL, src))) return err;
        value += len;
      }
      break;
    case CFGCLI_ARRAY_STR:
      *((char ***) par->var) = calloc(par->narr, sizeof(char *));
      if (!(*((char ***) par->var))) return CFGCLI_ERR_MEMORY;
      /* Allocate enough memory for the first element of the string array. */
      *(*((char ***) par->var)) = calloc(par->vlen, sizeof(char));
      char *tmp = *(*((char ***) par->var));
      if (!tmp) return CFGCLI_ERR_MEMORY;
      /* The rest elements point to different positions of the space. */
      for (i = 0; i < par->narr; i++) {
        (*((char ***) par->var))[i] = tmp;
        len = strlen(value) + 1;
        if ((err = cfgcli_get_value(&tmp, value, len, CFGCLI_DTYPE_STR, src)))
          return err;
        tmp += strlen(tmp) + 1;       /* null termination ensured by calloc */
        value += len;
      }
      break;
    default:
      return CFGCLI_ERR_DTYPE;             /* is CFGCLI_DTYPE_IS_ARRAY correct? */
  }
  return 0;
}

/******************************************************************************
Function `cfgcli_get`:
  Retrieve the parameter value and assign it to a variable.
Arguments:
  * `cfg`:      entry for all configurations;
  * `par`:      address of the verified configuration parameter;
  * `src`:      source of the value;
Return:
  Zero on success; non-zero on error.
******************************************************************************/
static int cfgcli_get(cfgcli_t *cfg, cfgcli_param_valid_t *par, int src) {
  int err = 0;
  /* Validate function arguments. */
  if (CFGCLI_IS_ERROR(cfg)) return CFGCLI_ERRNO(cfg);
  if (!par->value || *par->value == '\0') return 0;     /* value not set */

  /* Deal with arrays and scalars separately. */
  if (CFGCLI_DTYPE_IS_ARRAY(par->dtype))   /* force preprocessing the value */
    err = cfgcli_get_array(par, src);
  else {
    /* Allocate memory only for string. */
    if (par->dtype == CFGCLI_DTYPE_STR) {
      *((char **) par->var) = calloc(par->vlen, sizeof(char));
      if (!(*((char **) par->var))) err = CFGCLI_ERR_MEMORY;
    }

    /* Assign values to the variable.  */
    if (!err)
      err = cfgcli_get_value(par->var, par->value, par->vlen, par->dtype, src);
  }

  switch (err) {
    case 0:
      return 0;
    case CFGCLI_ERR_MEMORY:
      cfgcli_msg(cfg, "failed to allocate memory for parameter", par->name);
      return CFGCLI_ERRNO(cfg) = err;
    case CFGCLI_ERR_VALUE:
      cfgcli_msg(cfg, "invalid value for parameter", par->name);
      return CFGCLI_ERRNO(cfg) = err;
    case CFGCLI_ERR_PARSE:
      cfgcli_msg(cfg, "failed to parse the value for parameter", par->name);
      return CFGCLI_ERRNO(cfg) = err;
    case CFGCLI_ERR_DTYPE:
      cfgcli_msg(cfg, "invalid data type for parameter", par->name);
      return CFGCLI_ERRNO(cfg) = err;
    default:
      cfgcli_msg(cfg, "unknown error occurred for parameter", par->name);
      return CFGCLI_ERRNO(cfg) = err;
  }
}


/*============================================================================*\
                High-level functions for reading configurations
                    from command line options and text files
\*============================================================================*/

/******************************************************************************
Function `cfgcli_read_opts`:
  Parse command line options.
Arguments:
  * `cfg`:      entry for the configurations;
  * `argc`:     number of arguments passed via command line;
  * `argv`:     array of command line arguments;
  * `prior`:    priority of values set via command line options;
  * `optidx`:   position of the first unparsed argument.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int cfgcli_read_opts(cfgcli_t *cfg, const int argc, char *const *argv,
    const int prior, int *optidx) {
  /* Validate function arguments. */
  if (!cfg) return CFGCLI_ERR_INIT;
  if (CFGCLI_IS_ERROR(cfg)) return CFGCLI_ERRNO(cfg);
  if (cfg->npar <= 0 && cfg->nfunc <= 0) {
    cfgcli_msg(cfg, "no parameter or function has been registered", NULL);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INIT;
  }
  if (prior <= CFGCLI_SRC_NULL) {
    cfgcli_msg(cfg, "invalid priority for command line options", NULL);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
  }

  *optidx = 0;
  if (argc <= 0 || !argv || !(*argv)) return 0;
  int i;

  /* Start parsing command line options. */
  for (i = 1; i < argc; i++) {
    char *arg = argv[i];
    if (!(CFGCLI_IS_OPT(arg))) {           /* unrecognised option */
      cfgcli_msg(cfg, "unrecognised command line option", arg);
      continue;
    }

    int j;
    char *optarg = NULL;

    for (j = 0; j < CFGCLI_MAX_LOPT_LEN + 2; j++)  /* check if '=' exists */
      if (arg[j] == '\0' || arg[j] == CFGCLI_CMD_ASSIGN) break;
    if (arg[j] == '\0') {                       /* '=' is not found */
      j = i + 1;
      if (j < argc && !(CFGCLI_IS_OPT(argv[j]))) optarg = argv[++i];
    }
    else if (arg[j] == CFGCLI_CMD_ASSIGN) {        /* '=' is found */
      arg[j] = '\0';
      optarg = &arg[j + 1];
    }
    else {
      cfgcli_msg(cfg, "the command line option is too long", arg);
      return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_CMD;
    }

    cfgcli_param_valid_t *params = (cfgcli_param_valid_t *) cfg->params;
    cfgcli_func_valid_t *funcs = (cfgcli_func_valid_t *) cfg->funcs;
    enum { not_found, is_param, is_func } status;
    status = not_found;

    if (arg[1] != CFGCLI_CMD_FLAG) {               /* short option */
      for (j = 0; j < cfg->npar; j++) {
        if (arg[1] == params[j].opt) {
          status = is_param;
          break;
        }
      }
      if (status != is_param) {
        for (j = 0; j < cfg->nfunc; j++) {
          if (arg[1] == funcs[j].opt) {
            status = is_func;
            break;
          }
        }
      }
    }
    else if (arg[2] != '\0') {                  /* long option */
      for (j = 0; j < cfg->npar; j++) {
        if (params[j].lopt &&
            !strncmp(params[j].lopt, arg + 2, params[j].llen)) {
          status = is_param;
          break;
        }
      }
      if (status != is_param) {
        for (j = 0; j < cfg->nfunc; j++) {
          if (funcs[j].lopt &&
              !strncmp(funcs[j].lopt, arg + 2, funcs[j].llen)) {
            status = is_func;
            break;
          }
        }
      }
    }
    else {                                      /* parser termination */
      *optidx = j;                      /* for arg = "--", j = i + 1 */
      break;
    }

    if (status == is_func) {            /* call the command line function */
      if (optarg) cfgcli_msg(cfg, "omitting command line argument", optarg);
      if (funcs[j].called)
        cfgcli_msg(cfg, "the function has already been called with option", arg);
      else {
        funcs[j].func(funcs[j].args);   /* call the function */
        funcs[j].called = 1;
      }
    }
    else if (status == is_param) {      /* assign parameter value */
      /* Priority check. */
      if (CFGCLI_SRC_VAL(params[j].src) > prior) continue;
      else if (CFGCLI_SRC_VAL(params[j].src) == prior) {
        cfgcli_msg(cfg, "omitting duplicate entry of parameter", params[j].name);
        continue;
      }
      /* Command line arguments can be omitted for bool type variables. */
      if (!optarg || *optarg == '\0') {
        if (params[j].dtype == CFGCLI_DTYPE_BOOL) {
          params[j].value = "T";
          params[j].vlen = 2;
        }
        else {
          cfgcli_msg(cfg, "argument not found for option", arg);
          return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_CMD;
        }
      }
      else {
        params[j].value = optarg;       /* args are surely null terminated */
        params[j].vlen = strlen(optarg) + 1;    /* safe strlen */
      }
      /* Assign value to variable. */
      int err = cfgcli_get(cfg, params + j, CFGCLI_SRC_OF_OPT(prior));
      if (err) return err;
      params[j].src = CFGCLI_SRC_OF_OPT(prior);
    }
    else                                /* option not registered */
      cfgcli_msg(cfg, "unrecognised command line option", arg);
  }

  if (*optidx == 0) *optidx = i;
  return 0;
}

/******************************************************************************
Function `cfgcli_read_file`:
  Read configuration parameters from a file.
Arguments:
  * `cfg`:      entry for the configurations;
  * `fname`:    name of the input file;
  * `prior`:    priority of values read from this file.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int cfgcli_read_file(cfgcli_t *cfg, const char *fname, const int prior) {
  /* Validate function arguments. */
  if (!cfg) return CFGCLI_ERR_INIT;
  if (CFGCLI_IS_ERROR(cfg)) return CFGCLI_ERRNO(cfg);
  if (cfg->npar <= 0) {
    cfgcli_msg(cfg, "no parameter has been registered", NULL);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INIT;
  }
  if (!fname || *fname == '\0') {
    cfgcli_msg(cfg, "the input configuration file is not set", NULL);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
  }
  if (!(cfgcli_strnlen(fname, CFGCLI_MAX_FILENAME_LEN))) {
    cfgcli_msg(cfg, "invalid filename of the configuration file", NULL);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
  }
  if (prior <= CFGCLI_SRC_NULL) {
    cfgcli_msg(cfg, "invalid priority for configuration file", fname);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_INPUT;
  }

  FILE *fp = fopen(fname, "r");
  if (!fp) {
    cfgcli_msg(cfg, "cannot open the configuration file", fname);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_FILE;
  }

  /* Read file by chunk. */
  size_t clen = CFGCLI_STR_INIT_SIZE;
  char *chunk = calloc(clen, sizeof(char));
  if (!chunk) {
    fclose(fp);
    cfgcli_msg(cfg, "failed to allocate memory for reading file", fname);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_MEMORY;
  }

  size_t nline, nrest, nproc, cnt;
  char *key, *value;
  cfgcli_parse_state_t state = CFGCLI_PARSE_START;
  nline = nrest = nproc = 0;
  key = value = NULL;
  cfgcli_param_valid_t *params = (cfgcli_param_valid_t *) cfg->params;

  while ((cnt = fread(chunk + nrest, sizeof(char), clen - nrest, fp))) {
    char *p = (state == CFGCLI_PARSE_ARRAY_START) ? chunk + nproc : chunk;
    char *end = chunk + nrest + cnt;
    char *endl;
    if (cnt < clen - nrest) *end++ = '\n';      /* terminate the last line */

    /* Process lines in the chunk. */
    while ((endl = memchr(p, '\n', end - p))) {
      *endl = '\0';             /* replace '\n' by '\0' for line parser */
      nline += 1;

      /* Retrieve the keyword and value from the line. */
      char msg[CFGCLI_NUM_MAX_SIZE(size_t)];
      int j;
      cfgcli_parse_return_t status =
        cfgcli_parse_line(p, endl - p, &key, &value, state);

      switch (status) {
        case CFGCLI_PARSE_DONE:
          /* search for the parameter given the name */
          for (j = 0; j < cfg->npar; j++)
            if (!strncmp(key, params[j].name, params[j].nlen)) break;
          if (j == cfg->npar)           /* parameter not found */
            cfgcli_msg(cfg, "unregistered parameter name", key);
          else {
            /* priority check */
            if  (CFGCLI_SRC_VAL(params[j].src) < prior) {
              params[j].value = value;
              params[j].vlen = strlen(value) + 1;
              int err = cfgcli_get(cfg, params + j, prior);
              if (err) {
                free(chunk);
                fclose(fp);
                return err;
              }
              params[j].src = prior;
            }
            else if (CFGCLI_SRC_VAL(params[j].src) == prior)
              cfgcli_msg(cfg, "omitting duplicate entry of parameter", key);
          }
          /* reset states */
          key = value = NULL;
          state = CFGCLI_PARSE_START;
          break;
        case CFGCLI_PARSE_CONTINUE:        /* line continuation */
          *endl = ' ';                  /* remove line break */
          state = CFGCLI_PARSE_ARRAY_START;
          break;
        case CFGCLI_PARSE_ERROR:
          sprintf(msg, "%zu", nline);
          cfgcli_msg(cfg, "invalid configuration entry at line", msg);
#if __STDC_VERSION__ > 201710L
          [[fallthrough]];
#endif
        case CFGCLI_PARSE_PASS:
          state = CFGCLI_PARSE_START;
          break;
        default:
          free(chunk);
          fclose(fp);
          sprintf(msg, "%d", status);
          cfgcli_msg(cfg, "unknown line parser status", msg);
          return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_UNKNOWN;
      }
      p = endl + 1;
    }

    /* The chunk cannot hold a full line. */
    if (p == chunk) {
      size_t new_len = 0;
      if (clen >= CFGCLI_STR_MAX_DOUBLE_SIZE) {
        if (SIZE_MAX - CFGCLI_STR_MAX_DOUBLE_SIZE >= clen)
          new_len = clen + CFGCLI_STR_MAX_DOUBLE_SIZE;
      }
      else if (SIZE_MAX / 2 >= clen) new_len = clen << 1;
      if (!new_len) {                   /* overflow occurred */
        cfgcli_msg(cfg, "failed to allocate memory for reading the file", fname);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_MEMORY;
      }
      char *tmp = realloc(chunk, new_len);
      if (!tmp) {
        cfgcli_msg(cfg, "failed to allocate memory for reading the file", fname);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_MEMORY;
      }
      chunk = tmp;
      clen = new_len;
      nrest += cnt;
      continue;
    }

    /* Copy the remaining characters to the beginning of the chunk. */
    if (state == CFGCLI_PARSE_ARRAY_START) {       /* copy also parsed part */
      if (!key) {
        free(chunk);
        fclose(fp);
        cfgcli_msg(cfg, "unknown parser interruption", NULL);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_UNKNOWN;
      }
      /* `key` is the starting point of this effective line */
      nrest = end - key;
      nproc = p - key;
      memmove(chunk, key, nrest);
      /* shift `key` and `value` */
      if (value) value -= key - chunk;
      key = chunk;
    }
    else {                      /* copy only from the current position */
      nrest = end - p;
      memmove(chunk, p, nrest);
    }

    /* The chunk is full. */
    if (nrest == clen) {
      size_t new_len = 0;
      if (clen >= CFGCLI_STR_MAX_DOUBLE_SIZE) {
        if (SIZE_MAX - CFGCLI_STR_MAX_DOUBLE_SIZE >= clen)
          new_len = clen + CFGCLI_STR_MAX_DOUBLE_SIZE;
      }
      else if (SIZE_MAX / 2 >= clen) new_len = clen << 1;
      if (!new_len) {
        cfgcli_msg(cfg, "failed to allocate memory for reading the file", fname);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_MEMORY;
      }
      size_t key_shift = key ? key - chunk : 0;
      size_t value_shift = value ? value - chunk : 0;
      char *tmp = realloc(chunk, new_len);
      if (!tmp) {
        cfgcli_msg(cfg, "failed to allocate memory for reading the file", fname);
        return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_MEMORY;
      }
      chunk = tmp;
      clen = new_len;
      if (key) key = chunk + key_shift;
      if (value) value = chunk + value_shift;
    }
  }

  if (!feof(fp)) {
    cfgcli_msg(cfg, "unexpected end of file", fname);
    free(chunk);
    fclose(fp);
    return CFGCLI_ERRNO(cfg) = CFGCLI_ERR_FILE;
  }

  free(chunk);
  fclose(fp);
  return 0;
}


/*============================================================================*\
                 Functions for checking the status of variables
\*============================================================================*/

/******************************************************************************
Function `cfgcli_is_set`:
  Check if a variable is set via the command line or files.
Arguments:
  * `cfg`:      entry of all configurations;
  * `var`:      address of the variable.
Return:
  True if the variable is set; false otherwise.
******************************************************************************/
bool cfgcli_is_set(const cfgcli_t *cfg, const void *var) {
  if (!cfg || !var || !cfg->npar) return false;
  for (int i = 0; i < cfg->npar; i++) {
    cfgcli_param_valid_t *par = (cfgcli_param_valid_t *) cfg->params + i;
    if (par->var == var) {
      if (par->src != CFGCLI_SRC_NULL) return true;
      break;
    }
  }
  return false;
}

/******************************************************************************
Function `cfgcli_get_size`:
  Return the number of elements for the parsed array.
Arguments:
  * `cfg`:      entry of all configurations;
  * `var`:      address of the variable.
Return:
  The number of array elements on success; 0 on error.
******************************************************************************/
int cfgcli_get_size(const cfgcli_t *cfg, const void *var) {
  if (!cfg || !var || !cfg->npar) return 0;
  for (int i = 0; i < cfg->npar; i++) {
    cfgcli_param_valid_t *par = (cfgcli_param_valid_t *) cfg->params + i;
    if (par->var == var) {
      if (par->src != CFGCLI_SRC_NULL) return par->narr;
      break;
    }
  }
  return 0;
}


/*============================================================================*\
               Functions for clean-up and error message handling
\*============================================================================*/

/******************************************************************************
Function `cfgcli_destroy`:
  Release memory allocated for the configuration parameters.
Arguments:
  * `cfg`:      pointer to the entry of all configurations.
******************************************************************************/
void cfgcli_destroy(cfgcli_t *cfg) {
  if (!cfg) return;
  if (cfg->npar) free(cfg->params);
  if (cfg->nfunc) free(cfg->funcs);
  cfgcli_error_t *err = cfg->error;
  if (err->max) free(err->msg);
  free(cfg->error);
  free(cfg);
}

/******************************************************************************
Function `cfgcli_perror`:
  Print the error message if there is an error.
Arguments:
  * `cfg`:      entry of all configurations;
  * `fp`:       output file stream to write to;
  * `msg`:      string to be printed before the error message.
******************************************************************************/
void cfgcli_perror(const cfgcli_t *cfg, FILE *fp, const char *msg) {
  if (!cfg || !(CFGCLI_IS_ERROR(cfg))) return;
  const cfgcli_error_t *err = (cfgcli_error_t *) cfg->error;
  if (err->num <= 0 || !err->msg) return;
  const char *sep, *errmsg = err->msg;
  for (int i = 0; i < err->num - 1; i++) errmsg += strlen(errmsg) + 1;

  if (!msg || *msg == '\0') msg = sep = "";
  else sep = " ";
  fprintf(fp, "%s%s%s.\n", msg, sep, errmsg);
}

/******************************************************************************
Function `cfgcli_pwarn`:
  Print the warning messages if there is any, and clean the warnings.
Arguments:
  * `cfg`:      entry of all configurations;
  * `fp`:       output file stream to write to;
  * `msg`:      string to be printed before the error message.
******************************************************************************/
void cfgcli_pwarn(cfgcli_t *cfg, FILE *fp, const char *msg) {
  const char *sep;
  if (!msg || *msg == '\0') msg = sep = "";
  else sep = " ";

  if (!cfg) {
    fprintf(fp, "%s%sthe interface for configurations is not initialised.\n",
        msg, sep);
    return;
  }
  cfgcli_error_t *err = (cfgcli_error_t *) cfg->error;
  const int num = (CFGCLI_IS_ERROR(cfg)) ? err->num - 1 : err->num;
  if (num <= 0 || !err->msg) return;

  char *errmsg = err->msg;
  for (int i = 0; i < num; i++) {
    fprintf(fp, "%s%s%s.\n", msg, sep, errmsg);
    errmsg += strlen(errmsg) + 1;
  }

  /* Clean the warnings. */
  err->num -= num;
  err->len -= errmsg - err->msg;
  memmove(err->msg, errmsg, err->len);
}
