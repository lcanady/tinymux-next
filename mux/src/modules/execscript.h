/*! \file execscript.h
 * \brief ExecScript Module — run trusted game scripts from softcode.
 *
 * Provides the softcode function:
 *   execscript(scriptname[, arg1[, arg2[, ...]]])
 *
 * The script must live in the game's scripts/ subdirectory (relative to the
 * working directory).  Only alphanumeric characters, hyphens, underscores,
 * and dots are allowed in scriptname; path separators and ".." are rejected.
 *
 * Arguments are passed directly to execvp() — no shell interpretation.
 * Output is capped at LBUF_SIZE-1 bytes; execution timeout is 5 seconds.
 * Only Wizards may call this function (CA_WIZARD permission).
 */

#ifndef EXECSCRIPT_H
#define EXECSCRIPT_H

const MUX_CID CID_ExecScript = UINT64_C(0x000000036552B001);

interface mux_IExecScriptSinkControl : public mux_IUnknown
{
public:
    virtual void Unregistering(void) = 0;
};

class CExecScript : public mux_IExecScriptSinkControl, mux_IFunction
{
public:
    // mux_IUnknown
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    UINT32     AddRef(void) override;
    UINT32     Release(void) override;

    // mux_IExecScriptSinkControl
    void Unregistering(void) override;

    // mux_IFunction
    MUX_RESULT Call(unsigned int nKey, UTF8 *buff, UTF8 **bufc, dbref executor,
                    dbref caller, dbref enactor, int eval, UTF8 *fargs[],
                    int nfargs, const UTF8 *cargs[], int ncargs) override;

    CExecScript(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CExecScript();

private:
    UINT32                  m_cRef;
    mux_ILog               *m_pILog;
    mux_IFunctionsControl  *m_pIFunctionsControl;
};

class CExecScriptFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    UINT32     AddRef(void) override;
    UINT32     Release(void) override;

    // mux_IClassFactory
    MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv) override;
    MUX_RESULT LockServer(bool bLock) override;

    CExecScriptFactory(void);
    virtual ~CExecScriptFactory();

private:
    UINT32 m_cRef;
};

#endif // EXECSCRIPT_H
