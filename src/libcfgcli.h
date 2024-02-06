/*******************************************************************************
* libcfgcli.h: this file is part of the libcfgcli library.

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

#ifndef _LIBCFGCLI_H_
#define _LIBCFGCLI_H_

#include <stdio.h>
#include <stdbool.h>

/*============================================================================*\
                           Definitions for data types
\*============================================================================*/

typedef enum {
  CFGCLI_DTYPE_NULL,
  CFGCLI_DTYPE_BOOL,
  CFGCLI_DTYPE_CHAR,
  CFGCLI_DTYPE_INT,
  CFGCLI_DTYPE_LONG,
  CFGCLI_DTYPE_FLT,
  CFGCLI_DTYPE_DBL,
  CFGCLI_DTYPE_STR,
  CFGCLI_ARRAY_BOOL,
  CFGCLI_ARRAY_CHAR,
  CFGCLI_ARRAY_INT,
  CFGCLI_ARRAY_LONG,
  CFGCLI_ARRAY_FLT,
  CFGCLI_ARRAY_DBL,
  CFGCLI_ARRAY_STR
} cfgcli_dtype_t;

#define CFGCLI_DTYPE_INVALID(x)    ((x) < CFGCLI_DTYPE_BOOL || (x) > CFGCLI_ARRAY_STR)
#define CFGCLI_DTYPE_IS_ARRAY(x)   ((x) >= CFGCLI_ARRAY_BOOL && (x) <= CFGCLI_ARRAY_STR)

/*============================================================================*\
                         Definitions for string lengths
\*============================================================================*/
#define CFGCLI_MAX_NAME_LEN        128
#define CFGCLI_MAX_LOPT_LEN        128
#define CFGCLI_MAX_HELP_LEN        1024
#define CFGCLI_MAX_FILENAME_LEN    1024

/*============================================================================*\
                          Definitions for the formats
\*============================================================================*/
#define CFGCLI_SYM_EQUAL           '='
#define CFGCLI_SYM_ARRAY_START     '['
#define CFGCLI_SYM_ARRAY_END       ']'
#define CFGCLI_SYM_ARRAY_SEP       ','
#define CFGCLI_SYM_COMMENT         '#'
#define CFGCLI_SYM_NEWLINE         '\\'

#define CFGCLI_CMD_FLAG            '-'
#define CFGCLI_CMD_ASSIGN          '='


/*============================================================================*\
                         Definition of data structures
\*============================================================================*/

/* Main entry for all configuration parameters and command line functions. */
typedef struct {
  int npar;             /* number of verified configuration parameters  */
  int nfunc;            /* number of verified command line functions    */
  void *params;         /* data structure for storing parameters        */
  void *funcs;          /* data structure for storing function pointers */
  void *error;          /* data structure for storing error messages    */
} cfgcli_t;

/* Interface for registering configuration parameters. */
typedef struct {
  int opt;                      /* short command line option            */
  char *lopt;                   /* long command line option             */
  char *name;                   /* name of the parameter                */
  cfgcli_dtype_t dtype;            /* data type of the parameter           */
  void *var;                    /* variable for the retrieved value     */
  char *help;                   /* help message                         */
} cfgcli_param_t;

/* Interface for registering command line functions. */
typedef struct {
  int opt;                      /* short command line option            */
  char *lopt;                   /* long command line option             */
  void (*func) (void *);        /* pointer to the function              */
  void *args;                   /* pointer to the arguments             */
  char *help;                   /* help message                         */
} cfgcli_func_t;


/*============================================================================*\
                            Definition of functions
\*============================================================================*/

/******************************************************************************
Function `cfgcli_init`:
  Initialise the entry for all parameters and command line functions.
Return:
  The address of the structure.
******************************************************************************/
cfgcli_t *cfgcli_init(void);

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
int cfgcli_set_params(cfgcli_t *cfg, const cfgcli_param_t *param, const int npar);

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
int cfgcli_set_funcs(cfgcli_t *cfg, const cfgcli_func_t *func, const int nfunc);

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
    const int prior, int *optidx);

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
int cfgcli_read_file(cfgcli_t *cfg, const char *fname, const int prior);

/******************************************************************************
Function `cfgcli_is_set`:
  Check if a variable is set via the command line or files.
Arguments:
  * `cfg`:      entry of all configurations;
  * `var`:      address of the variable.
Return:
  True if the variable is set; false otherwise.
******************************************************************************/
bool cfgcli_is_set(const cfgcli_t *cfg, const void *var);

/******************************************************************************
Function `cfgcli_get_size`:
  Return the number of elements for the parsed array.
Arguments:
  * `cfg`:      entry of all configurations;
  * `var`:      address of the variable.
Return:
  The number of array elements on success; 0 on error.
******************************************************************************/
int cfgcli_get_size(const cfgcli_t *cfg, const void *var);

/******************************************************************************
Function `cfgcli_destroy`:
  Release memory allocated for the configuration parameters.
Arguments:
  * `cfg`:      pointer to the entry of all configurations.
******************************************************************************/
void cfgcli_destroy(cfgcli_t *cfg);

/******************************************************************************
Function `cfgcli_perror`:
  Print the error message if there is an error.
Arguments:
  * `cfg`:      entry of all configurations;
  * `fp`:       output file stream to write to;
  * `msg`:      string to be printed before the error message.
******************************************************************************/
void cfgcli_perror(const cfgcli_t *cfg, FILE *fp, const char *msg);

/******************************************************************************
Function `cfgcli_pwarn`:
  Print the warning messages if there is any, and clean the warnings.
Arguments:
  * `cfg`:      entry of all configurations;
  * `fp`:       output file stream to write to;
  * `msg`:      string to be printed before the error message.
******************************************************************************/
void cfgcli_pwarn(cfgcli_t *cfg, FILE *fp, const char *msg);

/******************************************************************************
Function `cfgcli_print_help`:
  Print help messages based on validated parameters
Arguments:
  * `cfg`:      entry for all configuration parameters;
******************************************************************************/
void cfgcli_print_help(cfgcli_t *cfg);
void cfgcli_print_usage(cfgcli_t *cfg, char *progname);

/******************************************************************************
Function `cfgcli_print_usage`:
  Print usage messages based on validated parameters and provided progname
Arguments:
  * `cfg`:      entry for all configuration parameters;
  * `progname`: program name to be displayed
******************************************************************************/
void cfgcli_print_usage(cfgcli_t *cfg, char *progname);

#endif
