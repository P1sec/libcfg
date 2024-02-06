/*******************************************************************************
* example.c: this file is an example for the usage of the libcfgcli library.

* libcfgcli: C library for parsing command line option and configuration files.

* Gitlab repository:
        https://framagit.org/groolot-association/libcfgclicli

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
#include <libcfgcli.h>

/* Default configuration file. */
#ifndef DEFAULT_CONF_FILE
#define DEFAULT_CONF_FILE "input.conf"
#endif
/* Priority of parameters from different sources. */
#define PRIOR_CMD               5
#define PRIOR_FILE              1
/* Print the warning and error messages. */
#define PRINT_WARNING   cfgcli_pwarn(cfg, stderr, "\x1B[35;1mWarning:\x1B[0m");
#define PRINT_ERROR     {                                               \
    cfgcli_perror(cfg, stderr, "\x1B[31;1mError:\x1B[0m");              \
    cfgcli_destroy(cfg);                                                \
    return 1;                                                           \
}

/* A function that prints help messages. */
void help(void *cfg) {
  cfgcli_print_usage((cfgcli_t *)cfg, NULL);
  cfgcli_print_help((cfgcli_t *)cfg);
  exit(0);
}

/* A function that prints the license information. */
void license(void *arg) {
  (void) arg;
  printf("This code is distributed under the MIT license.\n\
See https://framagit.org/groolot-association/libcfgcli/-/blob/master/LICENSE.txt\n");
  exit(0);
}


int main(int argc, char *argv[]) {
  int i, n, optidx = 0;

  /* Variables for storing the configurations. */
  char *fconf;          /* filename of the configuration file */
  bool vbool;
  char vchar;
  int vint;
  long vlong;
  float vflt;
  double vdbl;
  char *vstr;
  bool *abool;
  char *achar;
  int *aint;
  long *along;
  float *aflt;
  double *adbl;
  char **astr;
  cfgcli_t *cfg = cfgcli_init();

  /* Configurations for functions to be called via command line flags. */
  const int nfunc = 2;
  const cfgcli_func_t funcs[2] = {
    { 'h', "help", help, cfg, "Print this message and exit." },
    { 0, "license", license, NULL , NULL }
  };

  /* Configuration parameters. */
  const cfgcli_param_t params[] = {
    { 'c', "conf", "CONF_FILE", CFGCLI_DTYPE_STR, &fconf,
        "Set the configuration file."
    },
    { 'b', "bool", "BOOL", CFGCLI_DTYPE_BOOL, &vbool,
        "Set the boolean type BOOL."
    },
    { 'a', "char", "CHAR", CFGCLI_DTYPE_CHAR, &vchar,
        "Set the char type CHAR."
    },
    { 'i', "int", "INT", CFGCLI_DTYPE_INT, &vint,
        "Set the int type INT."
    },
    { 'l', "long", "LONG", CFGCLI_DTYPE_LONG, &vlong,
        "Set the long type LONG"
    },
    { 'f', "float", "FLOAT", CFGCLI_DTYPE_FLT, &vflt,
        "Set the float type FLOAT"
    },
    { 'd', "double", "DOUBLE", CFGCLI_DTYPE_DBL, &vdbl,
        "Set the double type DOUBLE"
    },
    { 's', "string", "STRING", CFGCLI_DTYPE_STR, &vstr,
        "Set the string type STRING"
    },
    { 'B', "bools", "BOOL_ARR", CFGCLI_ARRAY_BOOL, &abool,
        "Set the boolean type BOOL_ARR"
    },
    { 'A', "chars", "CHAR_ARR", CFGCLI_ARRAY_CHAR, &achar,
        "Set the char type CHAR_ARR"
    },
    { 'I', "ints", "INT_ARR", CFGCLI_ARRAY_INT, &aint,
        "Set the int type INT_ARR"
    },
    { 'L', "longs", "LONG_ARR", CFGCLI_ARRAY_LONG, &along,
        "Set the long type LONG_ARR"
    },
    { 'F', "floats", "FLOAT_ARR", CFGCLI_ARRAY_FLT, &aflt,
        "Set the float type FLOAT_ARR"
    },
    { 'D', "doubles", "DOUBLE_ARR", CFGCLI_ARRAY_DBL, &adbl,
        "Set the double type DOUBLE_ARR"
    },
    { 'S', "strings", "STRING_ARR", CFGCLI_ARRAY_STR, &astr,
        "Set the string type STRING_ARR"
    }
  };
  const int npar = sizeof(params) / sizeof(params[0]);
  (void) npar;

  /* Initialise the configuations. */
  if (!cfg) {
    fprintf(stderr, "Error: failed to initlise the configurations.\n");
    return 1;
  }

  /* Register configuration parameters. */
  if (cfgcli_set_params(cfg, params, npar)) PRINT_ERROR;
  PRINT_WARNING;

  /* Register functions to be called via command line options. */
  if (cfgcli_set_funcs(cfg, funcs, nfunc)) PRINT_ERROR;
  PRINT_WARNING;

  /* Parse command line options. */
  if (cfgcli_read_opts(cfg, argc, argv, PRIOR_CMD, &optidx)) PRINT_ERROR;
  PRINT_WARNING;

  /* Print command line arguments that are not parsed. */
  if (optidx < argc - 1) {
    printf("Unused command line options:\n ");
    for (i = optidx; i < argc; i++)
      printf(" %s", argv[i]);
    printf("\n");
  }

  /* Read configuration file. */
  if (!cfgcli_is_set(cfg, &fconf)) fconf = DEFAULT_CONF_FILE;
  if (cfgcli_read_file(cfg, fconf, PRIOR_FILE)) PRINT_ERROR;
  PRINT_WARNING;

  /* Print variables and release memory if necessary. */
  if (cfgcli_is_set(cfg, &vbool)) printf("BOOL  : %d\n", vbool);
  if (cfgcli_is_set(cfg, &vchar)) printf("CHAR  : %c\n", vchar);
  if (cfgcli_is_set(cfg, &vint)) printf("INT   : %d\n", vint);
  if (cfgcli_is_set(cfg, &vlong)) printf("LONG  : %ld\n", vlong);
  if (cfgcli_is_set(cfg, &vflt)) printf("FLOAT : %f\n", vflt);
  if (cfgcli_is_set(cfg, &vdbl)) printf("DOUBLE: %lf\n", vdbl);
  if (cfgcli_is_set(cfg, &vstr)) {
    printf("STRING: %s\n", vstr);
    free(vstr);
  }

  /* Print arrays and release memory if necessary. */
  if ((n = cfgcli_get_size(cfg, &abool))) {
    printf("BOOL ARRAY  : ");
    for (i = 0; i < n; i++) printf("| %d ", abool[i]);
    printf("|\n");
    free(abool);
  }
  if ((n = cfgcli_get_size(cfg, &achar))) {
    printf("CHAR ARRAY  : ");
    for (i = 0; i < n; i++) printf("| %c ", achar[i]);
    printf("|\n");
    free(achar);
  }
  if ((n = cfgcli_get_size(cfg, &aint))) {
    printf("INT ARRAY   : ");
    for (i = 0; i < n; i++) printf("| %d ", aint[i]);
    printf("|\n");
    free(aint);
  }
  if ((n = cfgcli_get_size(cfg, &along))) {
    printf("LONG ARRAY  : ");
    for (i = 0; i < n; i++) printf("| %ld ", along[i]);
    printf("|\n");
    free(along);
  }
  if ((n = cfgcli_get_size(cfg, &aflt))) {
    printf("FLOAT ARRAY : ");
    for (i = 0; i < n; i++) printf("| %f ", aflt[i]);
    printf("|\n");
    free(aflt);
  }
  if ((n = cfgcli_get_size(cfg, &adbl))) {
    printf("DOUBLE ARRAY: ");
    for (i = 0; i < n; i++) printf("| %lf ", adbl[i]);
    printf("|\n");
    free(adbl);
  }
  if ((n = cfgcli_get_size(cfg, &astr))) {
    printf("STRING ARRAY: ");
    for (i = 0; i < n; i++) printf("| %s ", astr[i]);
    printf("|\n");
    free(*astr);
    free(astr);
  }

  /* Release memory. */
  if (cfgcli_is_set(cfg, &fconf)) free(fconf);
  cfgcli_destroy(cfg);
  return 0;
}
