/*
    Poor Man's Hardware Reverse Engineering Tool script language interpreter
    Copyright (C) 2003 Andrew Zabolotny

    For conditions of use see file COPYING
*/

#include <windows.h> // CreateThread

#include <wchar.h>
#include <stdio.h> // fopen, FILE
#include <ctype.h> // isspace, toupper
#include <stdarg.h> // va_list
#include <string.h> // strchr, memcpy, memset
#include <stdlib.h> // free

#include "xtypes.h"
#include "cbitmap.h" // TEST/SET/CLEARBIT
#include "output.h" // Output, fnprepare
#include "exceptions.h" // TRY_EXCEPTION_HANDLER
#include "script.h"


/****************************************************************
 * Command/Variable setup
 ****************************************************************/

// Symbols added by linker.
extern "C" {
    extern commandBase *commands_start[];
    extern commandBase *commands_end;
}
#define commands_count (&commands_end - commands_start)

// Initialize builtin commands and variables.
void
setupCommands()
{
    for (int i = 0; i < commands_count; i++) {
        commandBase *x = commands_start[i];
        if (x->testAvail) {
            //Output("Testing for command %s", x->name);
            int ret = x->testAvail();
            if (!ret) {
                Output("Not registering command %s", x->name);
                continue;
            }
            Output("Registering command %s", x->name);
        }
        x->isAvail = 1;
    }
}


/****************************************************************
 * Basic variable definitions
 ****************************************************************/

// List of user defined variables.
static commandBase **UserVars = NULL;
static int UserVarsCount = 0;

// Locate a variable in a specified list.
static variableBase *
__findVar(const char *vn, commandBase **vars, int varCount)
{
    for (int i = 0; i < varCount; i++) {
        variableBase *var = variableBase::cast(vars[i]);
        if (var && !_stricmp(vn, var->name))
            return var;
    }
    return NULL;
}

// Lookup a variable (either in the predefined list or user added list).
variableBase *
FindVar(const char *vn)
{
    variableBase *v = __findVar(vn, commands_start, commands_count);
    if (v)
        return v;
    return __findVar(vn, UserVars, UserVarsCount);
}

// Create a new user defined variable.
static void AddVar(const char *name, variableBase *v, const char *desc=NULL)
{
    UserVars = (commandBase**)
        realloc(UserVars, sizeof(UserVars[0]) * (UserVarsCount + 1));
    UserVars[UserVarsCount++] = v;
    v->name = _strdup(name);
    if (! desc)
        v->desc = "User Variable";
    else
        v->desc = _strdup(desc);
    v->isAvail = 1;
}

// Translate a variable to an integer for argument parsing.
static bool GetVar(const char *vn, const char **s, uint32 *v)
{
    variableBase *var = FindVar(vn);
    if (!var)
        return false;
    return var->getVar(s, v);
}


/****************************************************************
 * Argument parsing
 ****************************************************************/

static char peek_char(const char **s)
{
    const char *x = *s;

    // Skip spaces at the beginning
    while (*x && isspace(*x))
        x++;

    *s = x;
    if (*x == '#')
        return 0;
    return *x;
}

static const char *quotes = "\"'";

// Extract the next argument as a string.
int
get_token(const char **s, char *storage, int storesize, int for_expr)
{
    const char *x = *s;

    // If at the end of string, return empty token
    if (! peek_char(&x)) {
        storage[0] = 0;
        return -1;
    }

    char quote = 0;
    if (strchr(quotes, *x))
        quote = *x++;

    const char *e = x;
    if (quote)
        while (*e && (*e != quote))
            e++;
    else if (for_expr)
        while (*e && (isalnum(*e) || (*e == '_')))
            e++;
    else
        while (*e && !isspace(*e))
            e++;

    if (e >= x + storesize)
        e = x + storesize - 1;
    memcpy(storage, x, e - x);
    storage[e - x] = 0;

    if (quote && *e)
        e++;
    *s = e;

    return 0;
}

// Extract the next argument as a wide character string.
int
get_wtoken(const char **args, wchar_t *storage, int storesize, int for_expr)
{
    char tmp[MAX_CMDLEN];
    int ret = get_token(args, tmp, sizeof(tmp));
    if (ret)
        return ret;
    mbstowcs(storage, tmp, storesize);
    return 0;
}

// Quick primitive expression evaluator
// Operation priorities:
// 0: ()
// 1: + - | ^
// 2: * / % &
// 3: == !=
// 4: unary+ unary- ~

// Expect to see a ')'
#define PAREN_EXPECT	1
// Eat the closing ')'
#define PAREN_EAT	2

// Parse the next part of the string as an expression
bool
get_expression(const char **s, uint32 *v, int priority, int flags)
{
  uint32 b;
  char store[MAX_CMDLEN];
  get_token(s, store, sizeof(store), 1);
  char *x = store;

  if (!*x)
  {
    // Got empty token, could be a unary operator or a parenthesis
    switch (peek_char (s))
    {
      case '(':
        (*s)++;
        if (!get_expression (s, v, 0, PAREN_EAT | PAREN_EXPECT))
          return false;
        break;

      case '+':
        (*s)++;
        if (!get_expression (s, v, 4, flags & ~PAREN_EAT))
          return false;
        break;

      case '-':
        (*s)++;
        if (!get_expression (s, v, 4, flags & ~PAREN_EAT))
          return false;
        *v = (uint32)-(int32)*v;
        break;

      case '!':
        (*s)++;
        if (!get_expression (s, v, 4, flags & ~PAREN_EAT))
          return false;
        *v = !*v;
        break;

      case '~':
        (*s)++;
        if (!get_expression (s, v, 4, flags & ~PAREN_EAT))
          return false;
        *v = ~*v;
        break;

      case 0:
      case ',':
        return false;

      default:
        ScriptError("Unexpected input '%s'", *s);
        return false;
    }
  }
  else
  {
    if (*x >= '0' && *x <= '9')
    {
      // We got a number
      char *err;
      *v = strtoul(x, &err, 0);
      if (*err)
      {
        ScriptError("Expected a number, got %s", x);
        return false;
      }
    }
    // Look through variables
    else if (!GetVar(x, s, v))
    {
      ScriptError("Unknown variable '%s' in expression", x);
      return false;
    }
  }

  // Peek next char and see if it is a operator
  bool unk_op = false;
  while (!unk_op)
  {
    char op = peek_char (s);
    if ((op == '=' || op == '!') && *(*s + 1) == '=') 
    {
      if (priority > 1)
        return true;
      *s += 2;
      if (!get_expression (s, &b, 1, flags & ~PAREN_EAT))
        return false;
      if (op == '=')
        *v = (*v == b);
      else
        *v = (*v != b);
      continue;
    }

    switch (op)
    {
      case '+':
      case '-':
      case '|':
      case '^':
        if (priority > 2)
          return true;
        (*s)++;
        if (!get_expression (s, &b, 2, flags & ~PAREN_EAT))
          return false;
        switch (op)
        {
          case '+': *v += b; break;
          case '-': *v -= b; break;
          case '|': *v |= b; break;
          case '^': *v ^= b; break;
        }
        break;

      case '*':
      case '/':
      case '%':
      case '&':
        if (priority > 3)
          return true;
        (*s)++;
        if (!get_expression (s, &b, 3, flags & ~PAREN_EAT))
          return false;
        switch (op)
        {
          case '*': *v *= b; break;
          case '/': *v /= b; break;
          case '%': *v %= b; break;
          case '&': *v &= b; break;
        }
        break;

      case ')':
        if (!(flags & PAREN_EXPECT))
        {
          ScriptError("Unexpected ')'");
          return false;
        }
        if (flags & PAREN_EAT)
          (*s)++;
        return true;

      default:
        unk_op = true;
        break;
    }
  }

  if (flags & PAREN_EXPECT)
  {
    ScriptError("No closing ')'");
    return false;
  }

  return true;
}

// Parser for integers (eg, "123") or an integer range (eg, "123..456")
bool
get_range(const char **s, uint32 *pstart, uint32 *pend)
{
    uint32 start, end;
    const char *tmp = *s;
    int ret = get_expression(&tmp, &start);
    if (!ret)
        return ret;
    if (tmp[0] != '.' || tmp[1] != '.') {
        // Only a single integer present.
        *s = tmp;
        *pend = *pstart = start;
        return ret;
    }
    // Looks like an integer range.
    tmp += 2;
    ret = get_expression(&tmp, &end);
    if (ret) {
        *s = tmp;
        if (start > end) {
            *pstart = end;
            *pend = start;
        } else {
            *pstart = start;
            *pend = end;
        }
    }
    return ret;
}

static bool get_args (const char **s, const char *keyw, uint32 *args, uint count)
{
  if (!count)
    return true;

  if (peek_char (s) != '(')
  {
    ScriptError("%s(%d args) expected", keyw, count);
    return false;
  }

  (*s)++;
  while (count--)
  {
    if (!get_expression (s, args, 0, count ? 0 : PAREN_EXPECT | PAREN_EAT))
    {
error:
      ScriptError("not enough arguments to function %s", keyw);
      return false;
    }

    if (!count)
      break;

    if (peek_char (s) != ',')
      goto error;

    (*s)++;
    args++;
  }

  return true;
}

// Convert a format and arg list on the command line into a string
// using snprintf.
int
arg_snprintf(char *buf, int len, const char *args)
{
    // Extract fmt and args
    char fmt[MAX_CMDLEN];
    get_token(&args, fmt, sizeof(fmt));
    uint32 fmtargs[4];
    for (uint i = 0; i < ARRAY_SIZE(fmtargs); i++)
        if (!get_expression(&args, &fmtargs[i]))
            break;

    // Build command string
    int rv = 0;
    TRY_EXCEPTION_HANDLER {
        _snprintf(buf, len, fmt, fmtargs[0], fmtargs[1], fmtargs[2], fmtargs[3]);
    } CATCH_EXCEPTION_HANDLER {
        Output(C_ERROR "EXCEPTION formatting '%s'", fmt);
        rv = -1;
    }
    return rv;
}

// Compare the token to a mask that separates the mandatory part from
// optional suffix with a '|', e.g. 'VD|UMP'
static bool IsToken (const char *tok, const char *mask)
{
  while (*tok && *mask && toupper (*tok) == *mask)
    tok++, mask++;

  if (!*tok && !*mask)
    return true;

  if (*mask != '|')
    return false;

  mask++;
  while (*tok && *mask && toupper (*tok) == *mask)
    tok++, mask++;

  if (*tok)
    return false;

  return true;
}


/****************************************************************
 * Script parsing
 ****************************************************************/

// Currently processed line (for error display)
static uint ScriptLine;

void ScriptError(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Output(C_ERROR "line %d: %s", ScriptLine, buf);
}

// Interpret one line of scripting language; returns false on QUIT
bool scrInterpret(const char *str, uint lineno)
{
    ScriptLine = lineno;

    const char *x = str;
    if (! peek_char(&x))
        return true;

    // Output command being executed to the log.
    Output(C_LOG "HaRET(%d)# %s", lineno, str);

    char tok[MAX_CMDLEN];
    get_token(&x, tok, sizeof(tok), 1);

    // Okay, now see what keyword is this :)
    for (int i = 0; i < commands_count; i++) {
        regCommand *hc = regCommand::cast(commands_start[i]);
        if (hc && IsToken(tok, hc->name)) {
            hc->func(tok, x);
            return true;
        }
    }

    if (IsToken(tok, "Q|UIT"))
        return false;

    Output(C_ERROR "Unknown keyword: `%s'", tok);
    return true;
}

// Run a haret script that is compiled into the exe.
void
runMemScript(const char *script)
{
    const char *s = script;
    for (int line = 1; *s; line++) {
        const char *lineend = strchr(s, '\n');
        const char *nexts;
        if (! lineend) {
            lineend = s + strlen(s);
            nexts = lineend;
        } else {
            nexts = lineend + 1;
        }
        if (lineend > s && lineend[-1] == '\r')
            lineend--;
        uint len = lineend - s;
        char str[MAX_CMDLEN];
        if (len >= sizeof(str))
            len = sizeof(str) - 1;
        memcpy(str, s, len);
        str[len] = 0;
        scrInterpret(str, line);
        s = nexts;
    }
}

// Execute the script from given file
void scrExecute (const char *scrfn, bool complain)
{
  char fn [100];
  fnprepare (scrfn, fn, sizeof (fn));

  FILE *f = fopen (fn, "r");
  if (!f)
  {
    if (complain)
      Output(C_ERROR "Cannot open script file\n%s", fn);
    return;
  }

  for (int line = 1; ; line++)
  {
    char str[MAX_CMDLEN];
    if (!fgets (str, sizeof (str), f))
      break;

    char *x = str + strlen(str);
    while ((x [-1] == '\n') || (x [-1] == '\r'))
      *(--x) = 0;

    scrInterpret(str, line);
  }

  fclose (f);
}


// Execute the script from given file
void scrExecute (const wchar_t *scrfn, bool complain)
{
  wchar_t fn [256];
  fnprepare (scrfn, fn, sizeof (fn));
  MessageBox(NULL, fn, L"Full script path", MB_OK);

  FILE *f = _wfopen (fn, L"r");
  if (!f)
  {
    if (complain)
      Output(C_ERROR "Cannot open script file\n%s", fn);
    return;
  }

  for (int line = 1; ; line++)
  {
    char str[MAX_CMDLEN];
    wchar_t wstr[MAX_CMDLEN];
    if (fgetws (wstr, sizeof (wstr), f) == NULL)
      break;

    
    size_t nConverted = wcstombs(str, wstr, sizeof(str));

    Output("precmd: %s",str);
    char *x = str + strlen(str);
    while ((x [-1] == '\n') || (x [-1] == '\r'))
      *(--x) = 0;

    Output("sufcmd: %s", str);
    scrInterpret(str, line);
  }

  fclose (f);
}

/****************************************************************
 * Variable definitions
 ****************************************************************/

regCommand *regCommand::cast(commandBase *b) {
    if (b && b->isAvail && strncmp(b->type, "cmd", 3) == 0)
        return static_cast<regCommand*>(b);
    return NULL;
}

dumpCommand *dumpCommand::cast(commandBase *b) {
    if (b && b->isAvail && strncmp(b->type, "dump", 4) == 0)
        return static_cast<dumpCommand*>(b);
    return NULL;
}

variableBase *variableBase::cast(commandBase *b) {
    if (b && b->isAvail && strncmp(b->type, "var", 3) == 0)
        return static_cast<variableBase*>(b);
    return NULL;
}
bool variableBase::getVar(const char **s, uint32 *v) {
    return false;
}
void variableBase::setVar(const char *s) {
    ScriptError("`%s' is a read-only variable", name);
}
void variableBase::showVar(const char *s) {
    uint32 val;
    bool res = getVar(&s, &val);
    if (res)
        Output("0x%08x", val);
    else
        ScriptError("Show operation on `%s' not supported", name);
}
void variableBase::clearVar(const char *s) {
    ScriptError("Can not clear `%s' variable", name);
}
void variableBase::fillVarType(char *buf) {
    strcpy(buf, &type[4]);
}
variableBase *variableBase::newVar() {
    return NULL;
}

bool integerVar::getVar(const char **s, uint32 *v) {
    *v = *data;
    return true;
}
void integerVar::setVar(const char *s) {
    if (!get_expression(&s, data))
        ScriptError("Expected numeric <value>");
}

bool stringVar::getVar(const char **s, uint32 *v) {
    *v = (uint32)*data;
    return true;
}
void stringVar::setVar(const char *s) {
    // If val_size is zero, it means a const char* in .text segment
    if (isDynamic)
        free(*data);
    char tmp[MAX_CMDLEN];
    get_token(&s, tmp, sizeof(tmp));
    *data = _strdup(tmp);
    isDynamic = 1;
}
void stringVar::showVar(const char *s) {
    Output("%s", *data);
}

bool bitsetVar::getVar(const char **s, uint32 *v) {
    if (!get_args(s, name, v, 1))
        return false;
    if (*v > maxavail) {
        ScriptError("Index out of range (0..%d)", maxavail);
        return false;
    }
    *v = TESTBIT(data, *v);
    return true;
}
void bitsetVar::setVar(const char *s) {
    uint32 idx, val;
    if (!get_expression(&s, &idx) || !get_expression(&s, &val)) {
        ScriptError("Expected <index> <value>");
        return;
    }
    if (idx > maxavail) {
        ScriptError("Index out of range (0..%d)", maxavail);
        return;
    }
    ASSIGNBIT(data, idx, val);
}

listVarBase *listVarBase::cast(commandBase *b) {
    if (b && b->isAvail && strncmp(b->type, "var_list", 8) == 0)
        return static_cast<listVarBase*>(b);
    return NULL;
}
bool listVarBase::getVar(const char **s, uint32 *v) {
    uint32 idx;
    if (!get_args(s, name, &idx, 1))
        return false;
    if (idx >= *count) {
        ScriptError("Index out of range (0..%d)", *count);
        return false;
    }
    void *p = (char *)data + datasize * idx;
    return getVarItem(p, s, v);
}
void listVarBase::setVar(const char *s) {
    uint32 idx;
    if (!get_args(&s, name, &idx, 1)) {
        ScriptError("Expected index for var %s", name);
        return;
    }
    if (idx >= *count) {
        ScriptError("Index out of range (0..%d)", *count);
        return;
    }
    void *p = (char *)data + datasize * idx;
    setVarItem(p, s);
}
void listVarBase::clearVar(const char *s) {
    *count = 0;
}
bool listVarBase::getVarItem(void *p, const char **s, uint32 *v) {
    return false;
}
bool listVarBase::setVarItem(void *p, const char *s) {
    ScriptError("`%s' is a read-only variable", name);
    return false;
}

bool intListVar::getVarItem(void *p, const char **s, uint32 *v) {
    *v = *(uint32*)p;
    return true;
}
bool intListVar::setVarItem(void *p, const char *s) {
    uint32 *d = (uint32*)p;
    if (! get_expression(&s, d)) {
        ScriptError("Expected <int>");
        return false;
    }
    return true;
}
void intListVar::showVar(const char *s) {
    uint32 *d = (uint32*)data;
    for (uint i=0; i<*count; i++)
        Output("%03d: 0x%08x", i, d[i]);
}

bool rofuncVar::getVar(const char **s, uint32 *v) {
    uint32 args[50];
    if (numargs && !get_args(s, name, args, numargs))
        return false;
    *v = func(false, args, 0);
    return true;
}
void rofuncVar::fillVarType(char *buf) {
    sprintf(buf, "%s(%d)", &type[4], numargs);
}

void rwfuncVar::setVar(const char *s) {
    uint32 val;
    uint32 args[50];
    if (!get_args(&s, name, args, numargs))
        return;
    if (!get_expression(&s, &val)) {
        ScriptError("Expected <value>");
        return;
    }
    func(true, args, val);
}

void SetVar(const char *name, const char *val)
{
    variableBase *var = FindVar(name);
    if (var) {
        var->setVar(val);
        return;
    }

    // Need to create a new user variable.
    integerVar *iv = new integerVar(0, "", 0, 0);
    iv->data = &iv->dynstorage;
    iv->setVar(val);
    AddVar(name, iv);
}

static void
cmd_set(const char *cmd, const char *args)
{
    char vn[MAX_CMDLEN];
    if (get_token(&args, vn, sizeof(vn), 1)) {
        ScriptError("Expected <varname>");
        return;
    }

    SetVar(vn, args);
}
REG_CMD(0, "S|ET", cmd_set,
        "SET <variable> <value>\n"
        "  Assign a value to a variable. Use HELP VARS for a list of variables.")

static void
cmd_newvar(const char *cmd, const char *args)
{
    char vn[MAX_CMDLEN], tn[MAX_CMDLEN];
    if (get_token(&args, vn, sizeof(vn), 1)
        || get_token(&args, tn, sizeof(tn), 1)) {
        ScriptError("Expected <varname>");
        return;
    }
    variableBase *var = FindVar(tn);
    if (!var) {
        ScriptError("Expected <varname>");
        return;
    }
    variableBase *newvar = FindVar(vn);
    if (newvar) {
        ScriptError("Variable already exists");
        return;
    }

    newvar = var->newVar();
    if (!newvar) {
        char type[variableBase::MAXTYPELEN];
        var->fillVarType(type);
        Output(C_WARN "Unable to clone variables of type %s", type);
        return;
    }

    char desc[MAX_CMDLEN];
    if (get_token(&args, desc, sizeof(desc)))
        AddVar(vn, newvar);
    else
        AddVar(vn, newvar, desc);
}
REG_CMD(0, "NEWVAR", cmd_newvar,
        "NEWVAR <varname> <template var> [<desc>]\n"
        "  Create a new variable of the same type as <template var>.  If\n"
        "  <desc> is specified the variable will show this description in\n"
        "  HELP VARS output.")

static void
cmd_showvar(const char *cmd, const char *args)
{
    char vn[MAX_CMDLEN];
    if (get_token(&args, vn, sizeof(vn), 1)) {
        ScriptError("Expected <varname>");
        return;
    }

    variableBase *var = FindVar(vn);
    if (!var) {
        ScriptError("Variable %s not found", vn);
        return;
    }
    if (toupper(cmd[0]) == 'C')
        // Clearvar
        var->clearVar(args);
    else
        // Showvar
        var->showVar(args);
}
REG_CMD(0, "SHOW|VAR", cmd_showvar,
        "SHOWVAR <variable>\n"
        "  Display the contents of a variable.")
REG_CMD_ALT(0, "CLEAR|VAR", cmd_showvar, clear,
        "CLEARVAR <variable>\n"
        "  Reset the contents of a variable.")

static void
cmd_addlist(const char *cmd, const char *args)
{
    char vn[MAX_CMDLEN];
    if (get_token(&args, vn, sizeof(vn), 1)) {
        ScriptError("Expected <varname>");
        return;
    }
    listVarBase *var = listVarBase::cast(FindVar(vn));
    if (!var) {
        ScriptError("Expected list variable");
        return;
    }

    if (*var->count >= var->maxavail) {
        Output("List %s already at max (%d)", var->name, var->maxavail);
        return;
    }
    void *p = (char *)var->data + var->datasize * (*var->count);
    int ret = var->setVarItem(p, args);
    if (ret)
        (*var->count)++;
}
REG_CMD(0, "ADDLIST", cmd_addlist,
        "ADDLIST <variable> <value>\n"
        "  Add an item to a list variable.");

static void
cmd_joinlist(const char *cmd, const char *args)
{
    char destname[MAX_CMDLEN];
    if (get_token(&args, destname, sizeof(destname), 1)) {
        ScriptError("Expected <dest list>");
        return;
    }
    commandBase *rawvar = FindVar(destname);
    listVarBase *destvar = listVarBase::cast(rawvar);
    if (!destvar) {
        if (rawvar) {
            ScriptError("Expected list variable");
            return;
        }
        const char *tmp = args;
        char peek[MAX_CMDLEN];
        if (get_token(&tmp, peek, sizeof(peek), 1)) {
            ScriptError("Expected <source list1>");
            return;
        }
        listVarBase *peekvar = listVarBase::cast(FindVar(peek));
        if (!peekvar) {
            ScriptError("Expected <source list1>");
            return;
        }
        destvar = static_cast<listVarBase*>(peekvar->newVar());
        if (!destvar) {
            ScriptError("Unable to allocate new variable %s", destname);
            return;
        }
        AddVar(destname, destvar);
    }

    for (;;) {
        char srcvarname[MAX_CMDLEN];
        if (get_token(&args, srcvarname, sizeof(srcvarname), 1))
            // Done
            return;
        rawvar = FindVar(srcvarname);
        if (!rawvar) {
            ScriptError("Expected <source list> instead of %s", srcvarname);
            return;
        }
        if (strcmp(rawvar->type, destvar->type) != 0) {
            ScriptError("Type mismatch on list variable %s", srcvarname);
            return;
        }
        listVarBase *srcvar = static_cast<listVarBase*>(rawvar);

        uint cnt = *srcvar->count;
        uint copycnt = min(cnt, destvar->maxavail - *destvar->count);
        void *p = (char *)destvar->data + destvar->datasize * (*destvar->count);
        memcpy(p, srcvar->data, destvar->datasize * copycnt);
        *destvar->count += copycnt;
        if (cnt != copycnt) {
            Output(C_WARN "Truncating list '%s' to %d"
                   , destname, destvar->maxavail);
            return;
        }
    }
}
REG_CMD(0, "JOINLIST", cmd_joinlist,
        "JOINLIST <dest list> <source list1> [<source list2> ...]\n"
        "  Append one or more lists to another list.  If <dest list> doesn't\n"
        "  exist it is created.");


/****************************************************************
 * Basic script commands
 ****************************************************************/

static void
cmd_dump(const char *cmd, const char *args)
{
    char vn[MAX_CMDLEN];
    if (get_token(&args, vn, sizeof(vn), 1)) {
        ScriptError("Dumper name expected");
        return;
    }

    for (int i = 0; i < commands_count; i++) {
        dumpCommand *hd = dumpCommand::cast(commands_start[i]);
        if (hd && !_stricmp(vn, hd->name)) {
            hd->func(vn, args);
            return;
        }
    }

    ScriptError("No dumper %s available, see HELP DUMP for a list", vn);
}
REG_CMD(0, "D|UMP", cmd_dump,
        "DUMP <hardware>[(args...)]\n"
        "  Dump the state of given hardware.\n"
        "  Use HELP DUMP to see available dumpers.")

class fileredir : public outputfn {
public:
    FILE *f;
    void sendMessage(const char *msg, int len) {
        fwrite(msg, len, 1, f);
    }
};

static void
redir(const char *args)
{
    char vn[MAX_CMDLEN];
    if (get_token(&args, vn, sizeof(vn))) {
        ScriptError("file name expected");
        return;
    }
    char fn[200];
    fnprepare(vn, fn, sizeof(fn));

    fileredir redir;
    redir.f = fopen(fn, "wb");
    if (!redir.f) {
        ScriptError("Cannot open file `%s' for writing", fn);
        return;
    }
    outputfn *old = setOutputFn(&redir);
    scrInterpret(args, ScriptLine);
    setOutputFn(old);
    fclose(redir.f);
}

static void
bgRun(char *args)
{
    prepThread();
    redir(args);
    free(args);
}

static void
cmd_redir(const char *cmd, const char *args)
{
    if (toupper(cmd[0]) == 'B')
        // Run in background thread.
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)bgRun,
                     (LPVOID)_strdup(args), 0, NULL);
    else
        redir(args);
}
REG_CMD(0, "REDIR", cmd_redir,
        "REDIR <filename> <command>\n"
        "  Run <command> and send it's output to <file>")
REG_CMD_ALT(0, "BG", cmd_redir, bg,
            "BG <filename> <command>\n"
            "  Run <command> in a background thread - store output in <file>")

static void
cmd_help(const char *cmd, const char *x)
{
    char vn[MAX_CMDLEN];
    get_token(&x, vn, sizeof(vn));

    if (!_stricmp(vn, "VARS")) {
        Output("Name                 Type");
        Output("-------------------- ----------");
        for (int i = 0; i < commands_count; i++) {
            variableBase *var = variableBase::cast(commands_start[i]);
            if (!var || !var->desc)
                continue;
            char type[variableBase::MAXTYPELEN];
            var->fillVarType(type);
            Output("%-20s %s\n  %s", var->name, type, var->desc);
        }
        for (int i = 0; i < UserVarsCount; i++) {
            variableBase *var = static_cast<variableBase*>(UserVars[i]);
            if (!var || !var->desc)
                continue;
            char type[variableBase::MAXTYPELEN];
            var->fillVarType(type);
            Output("%-20s %s\n  %s", var->name, type, var->desc);
        }
    }
    else if (!_stricmp(vn, "DUMP"))
    {
        for (int i = 0; i < commands_count; i++) {
            dumpCommand *hc = dumpCommand::cast(commands_start[i]);
            if (hc && hc->desc)
                Output("%s", hc->desc);
        }
    }
    else if (!vn[0])
    {
        Output("----=====****** A summary of HaRET commands: ******=====----");
        Output("Notations used below:");
        Output("  [A|B] denotes either A or B");
        Output("  <ABC> denotes a mandatory argument");
        Output("  Any command name can be shortened to minimal unambiguous length,");
        Output("  e.g. you can use 'p' for 'print' but not 'vd' for 'vdump'");
        for (int i = 0; i < commands_count; i++) {
            regCommand *hc = regCommand::cast(commands_start[i]);
            if (hc && hc->desc)
                Output("%s", hc->desc);
        }
        Output("QUIT");
        Output("  Quit the remote session.");
    }
    else
        Output("No help on this topic available");
}
REG_CMD(0, "H|ELP", cmd_help,
        "HELP [VARS|DUMP]\n"
        "  Display a description of either commands, variables or dumpers.")

static void
cmd_runscript(const char *cmd, const char *args)
{
    char vn[MAX_CMDLEN];
    if (get_token(&args, vn, sizeof(vn))) {
        ScriptError("file name expected");
        return;
    }
    uint32 ignore = 0;
    get_expression(&args, &ignore);

    scrExecute(vn, !ignore);
}
REG_CMD(0, "R|UNSCRIPT", cmd_runscript,
        "RUNSCRIPT <filename> [<ignoreNotFound>]\n"
        "  Run the commands located in the specified file.\n"
        "  Set <ignoreNotFound> to 1 to suppress a file not found error.")

static void
cmd_test(const char *cmd, const char *args)
{
    uint32 val;
    if (!get_expression(&args, &val)) {
        ScriptError("expected <expr>");
        return;
    }
    if (val)
        scrInterpret(args, ScriptLine);
}
REG_CMD(0, "IF", cmd_test,
        "IF <expr> <command>\n"
        "  Run <command> iff <expr> is non-zero.")

static void
cmd_evalf(const char *cmd, const char *args)
{
    // Build command string
    char cmdstr[MAX_CMDLEN];
    int ret = arg_snprintf(cmdstr, sizeof(cmdstr), args);
    if (ret)
        return;

    // Run command
    scrInterpret(cmdstr, ScriptLine);
}
REG_CMD(0, "EVALF", cmd_evalf,
        "EVALF <fmt> [<args>...]\n"
        "  Build a string based on <fmt> and <args> using sprintf and\n"
        "  then evaluate the string as a command")

static void
cmd_exit(const char *cmd, const char *args)
{
    shutdownHaret();
    exit(0);
}
REG_CMD(0, "EXIT", cmd_exit,
        "EXIT\n"
        "  Terminate HaRET process")
