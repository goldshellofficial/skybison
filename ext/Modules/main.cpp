// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <getopt.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <memory>

#include "cpython-data.h"
#include "cpython-func.h"

#include "builtins-module.h"
#include "exception-builtins.h"
#include "marshal.h"
#include "os.h"
#include "runtime.h"
#include "thread.h"
#include "vector.h"
#include "version.h"
#include "view.h"

namespace py {

extern Vector<const char*> warn_options;

static const char* const kInteractiveHelp =
    R"(Type "help", "copyright", "credits" or "license" for more information.)";

static const char* const kSupportedOpts = "+bBc:dEthiIm:OqsSuvVW:xX:";
static const struct option kSupportedLongOpts[] = {
    {"help", no_argument, nullptr, 'h'},
    {"version", no_argument, nullptr, 'V'},
    {nullptr, 0, nullptr, 0}};

static void failArgConversion(const char* message, int argi) {
  std::fprintf(stderr, "Fatal python error: %s #%i\n", message, argi);
  std::abort();
}

static void decodeArgv(int argc, const char* const* argv, wchar_t** wargv) {
  for (int i = 0; i < argc; i++) {
    wargv[i] = Py_DecodeLocale(argv[i], nullptr);
    if (wargv[i] == nullptr) {
      failArgConversion("unable to decode the command line argument", i + 1);
    }
  }
}

static void encodeWargv(int argc, const wchar_t* const* wargv, char** argv) {
  // TODO(T57811636): Support UTF-8 arguments on macOS.
  // We don't have UTF-8 decoding machinery that is decoupled from the
  // runtime. That's why we can't use Py_EncodeLocale() here.
  for (int i = 0; i < argc; i++) {
    const wchar_t* wc_str = wargv[i];
    size_t size = std::wcslen(wc_str);
    char* c_str = static_cast<char*>(PyMem_Malloc((size + 1) * sizeof(char)));
    for (size_t p = 0; p < size; p++) {
      wchar_t ch = wc_str[p];
      if (ch & 0x80) {
        UNIMPLEMENTED("UTF-8 argument support unimplemented");
      }
      c_str[p] = static_cast<char>(ch);
    }
    c_str[size] = '\0';
    argv[i] = c_str;
  }
}

static void runInteractiveHook() {
  Thread* thread = Thread::current();
  RawObject result = thread->invokeFunction0(ID(sys), ID(__interactivehook__));
  if (result.isErrorException()) {
    std::fprintf(stderr, "Failed calling sys.__interactivehook__\n");
    printPendingExceptionWithSysLastVars(thread);
    thread->clearPendingException();
  }
}

static int runModule(const char* modname_cstr, bool set_argv0) {
  Thread* thread = Thread::current();
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);

  Str runpy(&scope, runtime->symbols()->at(ID(runpy)));
  Object result(&scope,
                thread->invokeFunction1(ID(builtins), ID(__import__), runpy));
  if (result.isError()) {
    std::fprintf(stderr, "Could not import runpy module\n");
    printPendingException(thread);
    return -1;
  }

  Str modname(&scope, runtime->newStrFromCStr(modname_cstr));
  Bool alter_argv(&scope, Bool::fromBool(set_argv0));
  result = thread->invokeFunction2(ID(runpy), ID(_run_module_as_main), modname,
                                   alter_argv);
  if (result.isError()) {
    printPendingException(thread);
    return -1;
  }
  return 0;
}

static int tryRunPackage(Thread* thread, const char* path_cst) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  Str path(&scope, runtime->newStrFromCStr(path_cst));
  Object result(&scope, thread->invokeFunction1(ID(builtins),
                                                ID(_try_run_package), path));
  if (result.isErrorException()) {
    PyErr_Print();
    return -1;
  }
  // path was not a package.
  if (result.isNoneType()) return 0;
  // package executed fine.
  return 1;
}

static void runStartupFile(PyCompilerFlags* cf) {
  const char* startupfile = std::getenv("PYTHONSTARTUP");
  if (startupfile == nullptr || startupfile[0] == '\0') return;
  FILE* fp = std::fopen(startupfile, "r");
  if (fp != nullptr) {
    PyRun_SimpleFileExFlags(fp, startupfile, 0, cf);
    std::fclose(fp);
  } else {
    int saved_errno = errno;
    PySys_WriteStderr("Could not open PYTHONSTARTUP\n");
    errno = saved_errno;
    PyErr_SetFromErrnoWithFilename(PyExc_IOError, startupfile);
    PyErr_Print();
  }
  PyErr_Clear();
}

PY_EXPORT int Py_BytesMain(int argc, char** argv) {
  int print_version = 0;
  bool print_help = false;
  const char* command = nullptr;
  const char* module = nullptr;

  optind = 1;

  DCHECK(warn_options.empty(), "warn options should be empty");
  int option;
  while ((option = getopt_long(argc, argv, kSupportedOpts, kSupportedLongOpts,
                               nullptr)) != -1) {
    // -c and -m mark the end of interpreter options - all further
    // arguments are passed to the script
    if (option == 'c') {
      command = optarg;
      break;
    }
    if (option == 'm') {
      module = optarg;
      break;
    }

    wchar_t* woptarg;
    switch (option) {
      case 'b':
        Py_BytesWarningFlag++;
        break;
      case 'd':
        Py_DebugFlag++;
        break;
      case 'i':
        Py_InspectFlag++;
        Py_InteractiveFlag++;
        break;
      case 'I':
        Py_IsolatedFlag++;
        Py_NoUserSiteDirectory++;
        Py_IgnoreEnvironmentFlag++;
        break;
      case 'O':
        Py_OptimizeFlag++;
        break;
      case 'B':
        Py_DontWriteBytecodeFlag++;
        break;
      case 's':
        Py_NoUserSiteDirectory++;
        break;
      case 'S':
        Py_NoSiteFlag++;
        break;
      case 'E':
        Py_IgnoreEnvironmentFlag++;
        break;
      case 't':
        // ignored for backwards compatibility.
        break;
      case 'u':
        Py_UnbufferedStdioFlag = 1;
        break;
      case 'v':
        Py_VerboseFlag++;
        break;
      case 'x':
        UNIMPLEMENTED("skip first line");
        break;
      case 'h':
      case '?':
        print_help = true;
        break;
      case 'V':
        print_version++;
        break;
      case 'W':
        warn_options.push_back(optarg);
        break;
      case 'X':
        woptarg = Py_DecodeLocale(optarg, nullptr);
        PySys_AddXOption(woptarg);
        PyMem_RawFree(woptarg);
        woptarg = nullptr;
        break;
      case 'q':
        Py_QuietFlag++;
        break;
      default:
        UNREACHABLE("Unexpected value returned from getopt_long()");
    }
  }

  if (print_help) {
    UNIMPLEMENTED("command line help");
  }

  if (print_version) {
    std::printf("Python %s\n", print_version >= 2 ? Py_GetVersion() : kVersion);
    return 0;
  }

  char* filename = nullptr;
  if (command == nullptr && module == nullptr && optind < argc &&
      std::strcmp(argv[optind], "-") != 0) {
    filename = argv[optind];
  }

  bool is_interactive = Py_FdIsInteractive(stdin, nullptr);

  wchar_t* prog_name = Py_DecodeLocale(argv[0], nullptr);
  if (prog_name == nullptr) {
    failArgConversion("unable to decode the program name", 0);
  }
  Py_SetProgramName(prog_name);
  PyMem_RawFree(prog_name);

  Py_Initialize();

  if (!Py_QuietFlag &&
      (Py_VerboseFlag || (command == nullptr && filename == nullptr &&
                          module == nullptr && is_interactive))) {
    std::fprintf(stderr, "Python %s on %s\n", Py_GetVersion(),
                 Py_GetPlatform());
    if (!Py_NoSiteFlag) {
      std::fprintf(stderr, "%s\n", kInteractiveHelp);
    }
  }

  int wargc;
  wchar_t** wargv;
  if (command != nullptr || module != nullptr) {
    // Start arg list with "-c" or "-m" and omit command/module arg
    wargc = argc - optind + 1;
    const char** argv_copy =
        static_cast<const char**>(PyMem_RawCalloc(wargc, sizeof(*argv_copy)));
    argv_copy[0] = command != nullptr ? "-c" : "-m";
    for (int i = optind; i < argc; i++) {
      argv_copy[i - optind + 1] = argv[i];
    }
    wargv = static_cast<wchar_t**>(PyMem_RawCalloc(wargc, sizeof(*wargv)));
    decodeArgv(wargc, argv_copy, wargv);
    PyMem_RawFree(argv_copy);
  } else {
    wargc = argc - optind;
    wargv = static_cast<wchar_t**>(PyMem_RawCalloc(wargc, sizeof(*wargv)));
    decodeArgv(wargc, argv + optind, wargv);
  }
  PySys_SetArgv(wargc, wargv);
  for (int i = 0; i < wargc; i++) {
    PyMem_RawFree(wargv[i]);
  }
  PyMem_RawFree(wargv);

  PyCompilerFlags flags = _PyCompilerFlags_INIT;

  int returncode;
  if (command != nullptr) {
    returncode = PyRun_SimpleStringFlags(command, &flags) == 0 ? EXIT_SUCCESS
                                                               : EXIT_FAILURE;
  } else if (module != nullptr) {
    returncode = runModule(module, true) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  } else if (filename != nullptr) {
    Thread* thread = Thread::current();
    int run_package_result = tryRunPackage(thread, filename);
    if (run_package_result == 1) {
      returncode = EXIT_SUCCESS;
    } else if (run_package_result < 0) {
      returncode = EXIT_FAILURE;
    } else {
      // `filename` was not a package, run as single file.
      FILE* fp = std::fopen(filename, "r");
      if (fp == nullptr) {
        std::fprintf(stderr, "%s: can't open file '%s': [Errno %d] %s\n",
                     argv[0], filename, errno, std::strerror(errno));
        return 2;
      }
      returncode =
          PyRun_AnyFileExFlags(fp, filename, /*closeit=*/1, &flags) == 0
              ? EXIT_SUCCESS
              : EXIT_FAILURE;
    }
  } else {
    // filename == nullptr: read input from stdin
    if (is_interactive) {
      Py_InspectFlag = 0;  // do exit on SystemExit
      runStartupFile(&flags);
      runInteractiveHook();
    }
    returncode =
        PyRun_AnyFileExFlags(stdin, "<stdin>", /*closeit=*/0, &flags) == 0
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
  }

  if (Py_InspectFlag && is_interactive) {
    Py_InspectFlag = 0;
    runInteractiveHook();
    returncode = PyRun_AnyFileExFlags(stdin, "<stdin>", 0, &flags) == 0
                     ? EXIT_SUCCESS
                     : EXIT_FAILURE;
  }

  Py_Finalize();

  return returncode;
}

PY_EXPORT int Py_Main(int argc, wchar_t** wargv) {
  std::fprintf(stderr,
               "Py_Main(int, wchar_t**) is intended for Windows applications; "
               "consider using Py_BytesMain(int, char**) on POSIX");
  char** argv = static_cast<char**>(PyMem_RawCalloc(argc, sizeof(*argv)));
  encodeWargv(argc, wargv, argv);
  int res = Py_BytesMain(argc, argv);
  for (int i = 0; i < argc; i++) {
    PyMem_Free(argv[i]);
  }
  PyMem_RawFree(argv);
  return res;
}

}  // namespace py
