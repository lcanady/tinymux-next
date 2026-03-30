/*! \file execscript.cpp
 * \brief ExecScript Module — run trusted game scripts from softcode.
 *
 * See execscript.h for the full interface description.
 * \author Diablerie@COR (2026)
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "execscript.h"

// Permission constants (from command.h — duplicated to keep this module self-contained)
#ifndef CA_WIZARD
#define CA_WIZARD 0x00000002
#endif

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;

static mux_IExecScriptSinkControl *g_pIExecScriptSinkControl = nullptr;

static MUX_CLASS_INFO execscript_classes[] =
{
    {CID_ExecScript}
};
#define NUM_CLASSES (sizeof(execscript_classes) / sizeof(execscript_classes[0]))

// ---------------------------------------------------------------------------
// dlopen exports
// ---------------------------------------------------------------------------

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow()
{
    return (0 == g_cComponents && 0 == g_cServerLocks) ? MUX_S_OK : MUX_S_FALSE;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    if (CID_ExecScript != cid)
    {
        return MUX_E_CLASSNOTAVAILABLE;
    }

    CExecScriptFactory *pFactory = nullptr;
    try { pFactory = new CExecScriptFactory; } catch (...) {}
    if (nullptr == pFactory) return MUX_E_OUTOFMEMORY;

    const MUX_RESULT mr = pFactory->QueryInterface(iid, ppv);
    pFactory->Release();
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register()
{
    if (nullptr != g_pIExecScriptSinkControl) return MUX_S_OK;

    MUX_RESULT mr = mux_RegisterClassObjects(NUM_CLASSES, execscript_classes, nullptr);
    if (MUX_FAILED(mr)) return mr;

    mux_IExecScriptSinkControl *pISinkControl = nullptr;
    mr = mux_CreateInstance(CID_ExecScript, nullptr, UseSameProcess,
                            // We expose mux_IExecScriptSinkControl
                            // Query it by IUnknown first, then cast.
                            mux_IID_IUnknown,
                            reinterpret_cast<void **>(&pISinkControl));
    if (MUX_SUCCEEDED(mr))
    {
        g_pIExecScriptSinkControl = pISinkControl;
        pISinkControl = nullptr;
    }
    else
    {
        (void)mux_RevokeClassObjects(NUM_CLASSES, execscript_classes);
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister()
{
    if (nullptr != g_pIExecScriptSinkControl)
    {
        g_pIExecScriptSinkControl->Unregistering();
        g_pIExecScriptSinkControl->Release();
        g_pIExecScriptSinkControl = nullptr;
    }
    return mux_RevokeClassObjects(NUM_CLASSES, execscript_classes);
}

// ---------------------------------------------------------------------------
// Security helpers
// ---------------------------------------------------------------------------

#include "execscript_impl.h"

// ---------------------------------------------------------------------------
// Script execution
// ---------------------------------------------------------------------------

#define EXECSCRIPT_LBUF_SIZE  8000
#define EXECSCRIPT_TIMEOUT_S     5

// Execute game/scripts/<script_name> with argv[1..nargs] = fargs[1..nfargs-1].
// Returns true and fills `out` (null-terminated) with stdout on success.
// Returns false and sets `err_msg` on failure.
static bool run_script(const char *script_name,
                       char **fargs, int nfargs,
                       char *out, size_t out_max,
                       const char **err_msg)
{
    // Build script path: "scripts/<name>"
    char script_path[256];
    const int n = snprintf(script_path, sizeof(script_path), "scripts/%s", script_name);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(script_path))
    {
        *err_msg = "#-1 SCRIPT NAME TOO LONG";
        return false;
    }

    // Build argv: script_path, fargs[1..], nullptr
    // fargs[0] is script_name (already used), fargs[1..nfargs-1] are script args.
    const int argc = 1 + (nfargs > 1 ? nfargs - 1 : 0);
    const char **argv = new const char *[static_cast<size_t>(argc) + 1];
    argv[0] = script_path;
    for (int i = 1; i < argc; i++)
    {
        argv[i] = reinterpret_cast<const char *>(fargs[i]);
    }
    argv[argc] = nullptr;

    // Stdout pipe for script output.
    int pfd[2];
    if (pipe(pfd) != 0)
    {
        delete[] argv;
        *err_msg = "#-1 PIPE FAILED";
        return false;
    }

    // Error pipe: parent reads one byte to detect execvp failure.
    // FD_CLOEXEC on the write end means it closes automatically on successful
    // exec, giving the parent EOF; if exec fails we write a byte explicitly.
    int efd[2];
    if (pipe(efd) != 0)
    {
        ::close(pfd[0]); ::close(pfd[1]);
        delete[] argv;
        *err_msg = "#-1 PIPE FAILED";
        return false;
    }
    fcntl(efd[1], F_SETFD, FD_CLOEXEC);

    const pid_t pid = fork();
    if (pid < 0)
    {
        ::close(pfd[0]); ::close(pfd[1]);
        ::close(efd[0]); ::close(efd[1]);
        delete[] argv;
        *err_msg = "#-1 FORK FAILED";
        return false;
    }

    if (pid == 0)
    {
        // Child: set up stdout → pipe, stdin/stderr → /dev/null.
        ::close(pfd[0]);
        ::close(efd[0]); // child doesn't need read end of error pipe
        dup2(pfd[1], STDOUT_FILENO);
        ::close(pfd[1]);

        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO);  ::close(devnull);  }

        const int devnull2 = open("/dev/null", O_WRONLY);
        if (devnull2 >= 0) { dup2(devnull2, STDERR_FILENO); ::close(devnull2); }

        execvp(script_path, const_cast<char **>(argv));
        // execvp failed: signal the parent via the error pipe, then exit.
        // (efd[1] is still open here; FD_CLOEXEC only fires on successful exec.)
        const char err_byte = 1;
        (void)write(efd[1], &err_byte, 1);
        _exit(127);
    }

    // Parent
    delete[] argv;
    ::close(pfd[1]);
    ::close(efd[1]); // close write end; read below gets EOF when child's side closes

    // Detect exec failure: blocks until child exec'd (closing efd[1] via
    // FD_CLOEXEC) or writes a byte on failure.
    char err_byte = 0;
    (void)read(efd[0], &err_byte, 1);
    ::close(efd[0]);

    if (err_byte == 1)
    {
        // execvp failed: reap the child and report the error.
        int wstatus = 0;
        waitpid(pid, &wstatus, 0);
        ::close(pfd[0]);
        *err_msg = "#-1 SCRIPT NOT FOUND OR NOT EXECUTABLE";
        return false;
    }

    // exec succeeded: read stdout with timeout via select().
    size_t total = 0;
    bool   got_eof = false;
    const auto deadline_s = static_cast<time_t>(EXECSCRIPT_TIMEOUT_S);

    while (total < out_max - 1)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(pfd[0], &rfds);
        struct timeval tv{deadline_s, 0};

        const int sel = select(pfd[0] + 1, &rfds, nullptr, nullptr, &tv);
        if (sel <= 0) break; // timeout or error

        const ssize_t nr = read(pfd[0], out + total, out_max - 1 - total);
        if (nr <= 0) { got_eof = true; break; }
        total += static_cast<size_t>(nr);
    }
    out[total] = '\0';
    ::close(pfd[0]);

    // Strip trailing newlines for clean softcode output.
    while (total > 0 && (out[total - 1] == '\n' || out[total - 1] == '\r'))
        out[--total] = '\0';

    // Kill child if timed out; blocking waitpid handles both cases.
    int wstatus = 0;
    if (!got_eof) kill(pid, SIGKILL);
    waitpid(pid, &wstatus, 0);

    return true;
}

// ---------------------------------------------------------------------------
// CExecScript
// ---------------------------------------------------------------------------

CExecScript::CExecScript() : m_cRef(1), m_pILog(nullptr), m_pIFunctionsControl(nullptr)
{
    g_cComponents++;
}

MUX_RESULT CExecScript::FinalConstruct()
{
    MUX_RESULT mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess, IID_ILog,
                                       reinterpret_cast<void **>(&m_pILog));
    if (MUX_SUCCEEDED(mr))
    {
        bool fStarted;
        mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("CExecScript::FinalConstruct()"));
            m_pILog->end_log();
        }
    }

    mr = mux_CreateInstance(CID_Functions, nullptr, UseSameProcess, IID_IFunctionsControl,
                            reinterpret_cast<void **>(&m_pIFunctionsControl));
    if (MUX_SUCCEEDED(mr))
    {
        mux_IFunction *pIFunction = nullptr;
        mr = QueryInterface(IID_IFunction, reinterpret_cast<void **>(&pIFunction));
        if (MUX_SUCCEEDED(mr))
        {
            // execscript([scriptname[, arg1, arg2, ...]])
            // 0..MAX_ARG args, wizard-only (CA_WIZARD = 0x00000002)
            m_pIFunctionsControl->Add(0, T("EXECSCRIPT"), pIFunction,
                                      MAX_ARG, 0, MAX_ARG, 0, CA_WIZARD);
            pIFunction->Release();
            pIFunction = nullptr;
        }
    }
    return mr;
}

CExecScript::~CExecScript()
{
    if (nullptr != m_pILog)
    {
        bool fStarted;
        const MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("CExecScript::~CExecScript()"));
            m_pILog->end_log();
        }
        m_pILog->Release();
        m_pILog = nullptr;
    }

    if (nullptr != m_pIFunctionsControl)
    {
        m_pIFunctionsControl->Release();
        m_pIFunctionsControl = nullptr;
    }
    g_cComponents--;
}

MUX_RESULT CExecScript::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid || /* treat our sink control as primary */ iid == mux_IID_IUnknown)
    {
        *ppv = static_cast<mux_IExecScriptSinkControl *>(this);
    }
    else if (IID_IFunction == iid)
    {
        *ppv = static_cast<mux_IFunction *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CExecScript::AddRef()  { return ++m_cRef; }
UINT32 CExecScript::Release()
{
    if (--m_cRef == 0) { delete this; return 0; }
    return m_cRef;
}

void CExecScript::Unregistering()
{
    if (nullptr != m_pIFunctionsControl)
    {
        m_pIFunctionsControl->Release();
        m_pIFunctionsControl = nullptr;
    }
}

// ---------------------------------------------------------------------------
// mux_IFunction::Call  — the execscript() softcode function
// ---------------------------------------------------------------------------

static void safe_copy_str(const char *src, UTF8 *buff, UTF8 **bufc)
{
    if (src == nullptr) return;
    auto tp      = *bufc;
    const auto end = buff + EXECSCRIPT_LBUF_SIZE - 1;
    while (tp < end && *src) *tp++ = static_cast<UTF8>(*src++);
    *bufc = tp;
}

MUX_RESULT CExecScript::Call(unsigned int /*nKey*/,
                              UTF8 *buff, UTF8 **bufc,
                              dbref /*executor*/, dbref /*caller*/, dbref /*enactor*/,
                              int /*eval*/,
                              UTF8 *fargs[], int nfargs,
                              const UTF8 * /*cargs*/[], int /*ncargs*/)
{
    if (nfargs < 1 || fargs[0] == nullptr || fargs[0][0] == '\0')
    {
        safe_copy_str("#-1 EXECSCRIPT REQUIRES SCRIPT NAME", buff, bufc);
        return MUX_S_OK;
    }

    const char *script_name = reinterpret_cast<const char *>(fargs[0]);

    if (!safe_script_name(script_name))
    {
        safe_copy_str("#-1 INVALID SCRIPT NAME", buff, bufc);
        return MUX_S_OK;
    }

    char output[EXECSCRIPT_LBUF_SIZE];
    const char *err_msg = nullptr;

    if (!run_script(script_name,
                    reinterpret_cast<char **>(fargs), nfargs,
                    output, sizeof(output), &err_msg))
    {
        safe_copy_str(err_msg ? err_msg : "#-1 EXECSCRIPT FAILED", buff, bufc);
        return MUX_S_OK;
    }

    safe_copy_str(output, buff, bufc);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CExecScriptFactory
// ---------------------------------------------------------------------------

CExecScriptFactory::CExecScriptFactory() : m_cRef(1) {}
CExecScriptFactory::~CExecScriptFactory() = default;

MUX_RESULT CExecScriptFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid || mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CExecScriptFactory::AddRef()  { return ++m_cRef; }
UINT32 CExecScriptFactory::Release()
{
    if (--m_cRef == 0) { delete this; return 0; }
    return m_cRef;
}

MUX_RESULT CExecScriptFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    if (nullptr != pUnknownOuter) return MUX_E_NOAGGREGATION;

    CExecScript *p = nullptr;
    try { p = new CExecScript; } catch (...) {}
    if (nullptr == p) return MUX_E_OUTOFMEMORY;

    const MUX_RESULT mr_init = p->FinalConstruct();
    if (MUX_FAILED(mr_init)) { p->Release(); return mr_init; }

    const MUX_RESULT mr = p->QueryInterface(iid, ppv);
    p->Release();
    return mr;
}

MUX_RESULT CExecScriptFactory::LockServer(bool bLock)
{
    if (bLock) g_cServerLocks++; else g_cServerLocks--;
    return MUX_S_OK;
}
