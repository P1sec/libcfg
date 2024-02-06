# libcfgcli

This is a fork of initial author Cheng Zhao's [libcfg
library](https://github.com/cheng-zhao/libcfg).

## Table of Contents

-   [Introduction](#introduction)
-   [Compilation and linking](#compilation-and-linking)
-   [Getting started](#getting-started)
    -   [Initialisation](#initialisation)
    -   [Parameter registration](#parameter-registration)
    -   [Function registration](#function-registration)
    -   [Parsing command line options](#parsing-command-line-options)
    -   [Parsing configuration file](#parsing-configuration-file)
    -   [Result validation](#result-validation)
    -   [Releasing memory](#releasing-memory)
    -   [Error handling](#error-handling)

## Introduction

This is a simple library written in C, for parsing command line
options and plain configuration files. It can be used to retrieve
variables and arrays into runtime memory, or call functions indicated
by command line flags.

This library is compliant with the ISO C99 standard, and relies only
on the C standard library. It is originaly written by Cheng Zhao
(&#36213;&#25104;), and is distributed under the [MIT
license](LICENSE.txt).

It has been renamed to `libcfgcli` and now maintained by Gregory David.

## Compilation and linking

Since this library is tiny and portable, it is recommended to compile
the only two source files &mdash; `libcfgcli.h` and `libcfgcli.c`
&mdash; along with your own program. To this end, one only needs to
include the header `libcfgcli.h` in the source file for parsing
configurations:

```c
#include "libcfgcli.h"
```

## System library

You can also set this library as shared onto your system. Use the
standard autotools suite.

### Build and install the library

```
autoreconf -fi
./configure --prefix=/usr/local
make
sudo make install
```

Library header should now be installed in
`/usr/local/include/libcfgcli.h`.

Feel free to use `pkg-config` to access compilation flags.

### Use the shared library

Include the header in top of your code.
```c
#include <libcfgcli.h>
```

Compile with `pkg-config`:
```
gcc $(pkg-config --cflags --libs cfgcli) -o example example.c
```

Compile without ~`pkg-config`~:
```
gcc -I/usr/local/include -lcfgcli -o example example.c
```

## Getting started

### Initialisation

A `cfgcli_t` type structure has to be initialised in the first place, for
storing all the configuration information. This can be done using the
function `cfgcli_init`, e.g

```c
cfgcli_t *config_ptr = cfgcli_init();
```

This function returns the `NULL` pointer on error.

### Parameter registration

To retrieve variables and arrays from command line options or
configuration files, they need to be registered as configuration
parameters, which are represented by the `cfgcli_param_t` type
structure:

```c
typedef struct {
  int opt;                      /* short command line option            */
  char *lopt;                   /* long command line option             */
  char *name;                   /* name of the parameter                */
  cfgcli_dtype_t dtype;         /* data type of the parameter           */
  void *var;                    /* variable for the retrieved value     */
  char *help;                   /* help message for the parameter       */
} cfgcli_param_t;
```

The format of the attributes are:
-   `opt`: a single case-sensitive letter or `0`;
-   `lopt`: a string composed of characters with graphical
    representations (ensured by
    [isgraph](https://en.cppreference.com/w/c/string/byte/isgraph)),
    or a `NULL` pointer;
-   `name`: a string composed of case-sensitive letters, digits, and
    the underscore character, and starting with either a letter or an
    underscore;
-   `dtype`: a pre-defined data type indicator;
-   `var`: pointer to the address of the variable/array for holding
    the retrieved value, and no memory allocation is needed;
-   `help`: a string defining the help message for the parameter.

In particular, if `opt` is set to `0`, or `lopt` is set to `NULL`, the
value will not be retrieved from short or long command line options,
respectively. For safety consideration, the length of `lopt` and
`name` must be smaller than the pre-defined
[`CFGCLI_MAX_LOPT_LEN`](libcfgcli.h#L66) and
[`CFGCLI_MAX_NAME_LEN`](libcfgcli.h#L65) values respectively.

The supported data types and their indicators are listed below:

| Data type                              | Indicator           | Native C type |
|----------------------------------------|---------------------|---------------|
| Boolean variable                       | `CFGCLI_DTYPE_BOOL` | `bool`        |
| Character variable                     | `CFGCLI_DTYPE_CHAR` | `char`        |
| Integer variable                       | `CFGCLI_DTYPE_INT`  | `int`         |
| Long integer variable.                 | `CFGCLI_DTYPE_LONG` | `long`        |
| Single-precision floating-point number | `CFGCLI_DTYPE_FLT`  | `float`       |
| Double-precision floating-point number | `CFGCLI_DTYPE_DBL`  | `double`      |
| String variable                        | `CFGCLI_DTYPE_STR`  | `char *`      |
| Boolean array                          | `CFGCLI_ARRAY_BOOL` | `bool *`      |
| Character array                        | `CFGCLI_ARRAY_CHAR` | `char *`      |
| Integer array                          | `CFGCLI_ARRAY_INT`  | `int *`       |
| Long integer array                     | `CFGCLI_ARRAY_LONG` | `long *`      |
| Single-precision floating-point array  | `CFGCLI_ARRAY_FLT`  | `float *`     |
| Double-precision floating-point array  | `CFGCLI_ARRAY_DBL`  | `double *`    |
| String array                           | `CFGCLI_ARRAY_STR`  | `char **`     |

Once the configuration parameters are set, they can be registered
using the function

```c
int cfgcli_set_params(cfgcli_t *cfg, const cfgcli_param_t *params, const int npar);
```

Here, `cfg` indicates the structure for storing all configuration
information, or the entry for the registration. `params` denotes the
address of the configuration parameter structure, and `npar` indicates
the number of parameters to be registered at once. This function
returns `0` on success, and a non-zero integer on error.

Note that the `cfgcli_param_t` type structure for parameter
registration cannot be deconstructed until the command line options
and configuration files containing this parameter are parsed (see
[Parsing command line options](#parsing-command-line-options) and
[Parsing configuration file](#parsing-configuration-file)),

### Function registration

Functions that can be called with command line flags must be declared
with the prototype

```c
void func(void *args);
```

Similar to configuration parameters, functions can also be registered
in the form of a structure:

```c
typedef struct {
  int opt;                      /* short command line option            */
  char *lopt;                   /* long command line option             */
  void (*func) (void *);        /* pointer to the function              */
  void *args;                   /* pointer to the arguments             */
  char *help;                   /* help message for the function        */
} cfgcli_func_t;
```

The `opt` and `lopt` variables are the short and long command line
option for calling this function, respectively. And at least one of
them has to be set, i.e., a case-sensitive letter for `opt`, or a
string composed of graphical characters for `lopt`. Again, the length
of `lopt` must be smaller than the pre-defined
[`CFGCLI_MAX_LOPT_LEN`](libcfgcli.h#L66) limit. The pointers `func`
and `args` are the address of the function to be called, and the
corresponding arguments, respectively. `help` is a string defining the
help message for the function.

The functions can then be registered using

```c
int cfgcli_set_funcs(cfgcli_t *cfg, const cfgcli_func_t *funcs, const int nfunc);
```

Here, `cfg` indicates the entry for the registration, `funcs` denotes
the address to the structure holding the registration information of
functions, and `nfunc` indicates the number of functions to be
registered at once. This function returns `0` on success, and a
non-zero integer on error.

Note that the `cfgcli_func_t` type structure for function registration
cannot be deconstructed until the command line options are parsed (see
[Parsing command line options](#parsing-command-line-options)).

As an example, a typical demand for calling functions via command line
is to print the usage of a program, when there is the `-h` or `--help`
flag. In this case, the help function and the corresponding structure
can be defined as

```c
void help(void *cfg) {
  cfgcli_print_help((cfgcli_t *)cfg);
  exit(0);
}
```

and registered with

```c
cfgcli_t *cfg = init_config();
...
const cfgcli_func_t help_func = {'h', "help", help, cfg, "Print this message and exit."};
```

### Parsing command line options

Command line options are passed to the `main` function at program
startup, as the `argc` and `argv` parameters. These two parameters can
be used by the function `cfgcli_read_opts` for parsing the options,
and retrieving parameter values or calling functions. This function is
defined as

```c
int cfgcli_read_opts(cfgcli_t *cfg, const int argc, char *const *argv, const int priority, int *optidx);
```

It returns `0` on success, and a non-zero integer on error. And the
arguments are:
-   `cfg`: the structure for all configuration information;
-   `argc`: the number of command line arguments, obtained from
    `main`;
-   `argv`: the command line argument list, obtained from `main`;
-   `priority`: the priority of values retrieved from command line,
    must be positive;
-   `optidx`: the index of the argument list at which the parser is
    terminated.

In particular, `priority` decides which value to take if the parameter
is defined in multiple sources, say, both in a configuration file and
command line options. For instance, if a variable has already been set
in a configuration file with a lower `priority` value than the one
passed to `cfgcli_read_opts`, then its value will be overwritten by
the one obtained from command line.

The supported formats of command line options are listed below:

| Description       | Format                                   | Example  | Note                                                                                                                                        |
|-------------------|------------------------------------------|----------|---------------------------------------------------------------------------------------------------------------------------------------------|
| Short option      | `-OPT VALUE`<br>or<br>`-OPT=VALUE`       | `-n=10`  | `OPT` must be a letter;<br>`VALUE` is optional.                                                                                             |
| Long option       | `--LOPT VALUE` <br>or<br> `--LOPT=VALUE` | `--help` | `LOPT` is a string with graphical characters,<br>with length smaller than [`CFGCLI_MAX_LOPT_LEN`](libcfgcli.h#L66);<br>`VALUE` is optional. |
| Option terminator | `--`                                     |          | It terminates option scanning.                                                                                                              |

Note that the `-` and `=` symbols in the formats are
customisable. They are actually defined as
[`CFGCLI_CMD_FLAG`](libcfgcli.h#L79) and
[`CFGCLI_CMD_ASSIGN`](libcfgcli.h#L80) in `libcfgcli.h`, respectively.

All command line arguments satisfying the above formats are
interpreted as options, otherwise they are treated as values. And
values can only be omitted for boolean type variables &mdash; which
implies `true` &mdash; or function calls. If the value contains space
or special characters that are reserved by the environment, then it
should be enclosed by pairs of single or double quotation
marks. Besides, values that may be confused with options (such as
`-x`) are recommended to be passed with the assignment symbol `=`.

Furthermore, if the `--` option is found, then the option scanning
will be terminated, and the current index of the argument list is
reported as `optidx`. Therefore, when calling the program, non-option
parameters should always be passed after all the options. And when
`optidx` is equal to `argc`, it means that all command line arguments
are parsed.

### Parsing configuration file

Plain text files can be parsed using the function

```c
int cfgcli_read_file(cfgcli_t *cfg, const char *filename, const int priority);
```

The argument `cfg` indicates the structure for storing all
configuration information, `filename` denotes the name of the input
file, and `priority` defines the priority of values read from this
file. This function returns `0` on success, and a non-zero integer on
error.

By default the format of a valid configuration file has to be

```nginx
# This is a comment.
name_variable = value                # inline comment
name_array = [ element1, element2 ]  # entry for an array
```

Here, `name_variable` and `name_array` indicate the registered name of
configuration parameters (see [Parameter
registration](#parameter-registration)), and `value`, `element1`, and
`element2` are the values to be loaded into memory.

In particular, scalar type definitions can be parsed as arrays with a
single element. And by default array type definitions with multiple
elements have to be enclosed by a pair of brackets `[]`. In addition,
multiple-line definitions are only allowed for arrays, and the line
break symbol `\` can only be placed after the array element separator
`,`. These symbols, including `[`, `]`, `\`, `,`, as well as the
comment indicator `#`, are customisable in
[libcfgcli.h](libcfgcli.h#L70). And if a value or an element of an
array contains special characters, the full value or element has to be
enclosed by a pair of single or double quotation marks.

### Result validation

The functions `cfgcli_read_opts` and `cfgcli_read_file` extract the
parameter value from command line options and configuration files
respectively. The value is then converted to the given data type, and
passed to the address of the variable specified at registration.

To verify whether a variable or an array is set correctly, one can use
the function

```c
bool cfgcli_is_set(const cfgcli_t *cfg, const void *var);
```

It returns `true` if the variable or array values are set by the
functions `cfgcli_read_opts` or `cfgcli_read_file`, and `false` if
they are untouched. Here, `var` has to be the variable/array address
specified at registration.

Moreover, the number of array elements read by the parser can be
reported by the function

```c
int cfgcli_get_size(const cfgcli_t *cfg, const void *var);
```

It returns `0` if the array is not set. So it may not be necessary to
verify arrays using `cfgcli_is_set`. Note that the array is allocated
with precisely the number of elements reported by this function, so
the indices for accessing array elements must be smaller than this
number.

Once the variable or array is verified successfully, it can then be
used directly in the rest parts of the program.

### Releasing memory

Once all the variables and arrays are retrieved and verified, the
`cfgcli_t` type structure for storing all the configuration
information can be deconstructed by the function

```c
void cfgcli_destroy(cfgcli_t *cfg);
```

After calling this function, the values of the variables and arrays
are still accessible, but the size of arrays cannot be obtained using
the `cfgcli_get_size` function anymore.

In addition, since the memory of arrays and string variables are
allocated by this library, it is the user's responsibility to free
them using the standard `free` function. In particular, since string
arrays are represented two-dimensional character arrays, the pointers
to both the string array and its first element have to be freed, e.g.

```c
char **str;             /* declaration of the string array */

/* parameter registration and retriving */

free(*str);             /* free the first element of the array */
free(str);              /* free the array itself */
```

### Error handling

Errors can be caught by checking the return values of some of the
functions, such as `cfgcli_init`, `cfgcli_set_params`,
`cfgcli_read_opts`, etc. And once the `cfgcli_init` is executed
successfully, error messages can be printed using the function

```c
void cfgcli_perror(const cfgcli_t *cfg, FILE *stream, const char *msg);
```

It outputs the string indicated by `msg`, followed by a colon and a
space, and then followed by the error message produced by this
library, as well as a newline character `\n`. The results are written
to `stream`, which is typically `stderr`.

Unexpected issues that are not critical enough to stop the program are
treated as warnings. They cannot be handled by the return values of
functions, but one can check warning messages using the function

```c
void cfgcli_pwarn(cfgcli_t *cfg, FILE *stream, const char *msg);
```

This function is similar to `cfgcli_perror`. Note that there can be
multiple warning messages, and once a warning message is printed, it
is automatically removed from the message pool.

### Examples

An example for the usage of this library is provided in the
[tests](tests) folder.

It registers variables and arrays for all the supported data types, as
well as two functions to be called via command line (`--help` and
`--license`). Command line options and the configuration file
[`input.conf`](tests/input.conf) are then parsed. The variables and
arrays are printed if they are set correctly.
