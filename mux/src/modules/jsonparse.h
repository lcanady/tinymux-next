/*! \file jsonparse.h
 * \brief JsonParse Module — JSON manipulation functions for softcode.
 *
 * Registers the following softcode functions (all Wizard-usable):
 *
 *   json_get(json, path)         — value at dot/bracket path, unwrapped if string
 *   json_keys(json[, path])      — space-separated object key list
 *   json_array_len(json[, path]) — integer length of array
 *   json_type(json[, path])      — "object"|"array"|"string"|"number"|"bool"|"null"
 *   json_encode(value)           — JSON-escape a string (adds surrounding quotes)
 *   json_decode(json_string)     — strip quotes and unescape a JSON string value
 *
 * Path syntax examples:
 *   "name"            top-level key
 *   "vitals.hp"       nested object key
 *   "items[0]"        first array element
 *   "items[0].name"   field of first array element
 */

#ifndef JSONPARSE_H
#define JSONPARSE_H

const MUX_CID CID_JsonParse = UINT64_C(0x000000036552C001);

// nKey values for mux_IFunction::Call
enum {
    JSONKEY_GET       = 0,
    JSONKEY_KEYS      = 1,
    JSONKEY_ARRAY_LEN = 2,
    JSONKEY_TYPE      = 3,
    JSONKEY_ENCODE    = 4,
    JSONKEY_DECODE    = 5
};

interface mux_IJsonParseSinkControl : public mux_IUnknown
{
public:
    virtual void Unregistering(void) = 0;
};

class CJsonParse : public mux_IJsonParseSinkControl, mux_IFunction
{
public:
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    UINT32     AddRef(void) override;
    UINT32     Release(void) override;

    void       Unregistering(void) override;

    MUX_RESULT Call(unsigned int nKey, UTF8 *buff, UTF8 **bufc,
                    dbref executor, dbref caller, dbref enactor, int eval,
                    UTF8 *fargs[], int nfargs,
                    const UTF8 *cargs[], int ncargs) override;

    CJsonParse(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CJsonParse();

private:
    UINT32                 m_cRef;
    mux_ILog              *m_pILog;
    mux_IFunctionsControl *m_pIFunctionsControl;
};

class CJsonParseFactory : public mux_IClassFactory
{
public:
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    UINT32     AddRef(void) override;
    UINT32     Release(void) override;
    MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv) override;
    MUX_RESULT LockServer(bool bLock) override;

    CJsonParseFactory(void);
    virtual ~CJsonParseFactory();

private:
    UINT32 m_cRef;
};

#endif // JSONPARSE_H
