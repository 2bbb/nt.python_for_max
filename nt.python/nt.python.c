/**
	@file
	nt.python - run python script on max
    Nao Tokui  www.naotokui.net
 
	@ingroup	
*/

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object

#include <Python/Python.h>


////////////////////////// constants

#define VERSION_STRING "0.10 beta"
#define CREDIT_STRING "nao tokui www.naotokui.net 2017"


////////////////////////// object struct
typedef struct _ntpython
{
	t_object ob;			// the object itself (must be first)
	void     *outlet, *outlet2;
    
    PyObject *t_module;
    char    *t_modulename;
} t_ntpython;

///////////////////////// function prototypes
//// standard set
void *ntpython_new(t_symbol *s, long argc, t_atom *argv);
void ntpython_free(t_ntpython *x);
void ntpython_assist(t_ntpython *x, void *b, long m, long a, char *s);
void ntpython_read(t_ntpython *x, t_symbol *s);
void ntpython_doread(t_ntpython *x, t_symbol *s, long argc, t_atom *argv);
void ntpython_anything(t_ntpython *x, t_symbol *s, long argc, t_atom *argv);
void ntpython_reload(t_ntpython *x);
void ntpython_doreload(t_ntpython *x, t_symbol *s, long argc, t_atom *argv);
void ntpython_bang(t_ntpython *x);

// UTILITIES
void print_python_error_message(t_ntpython *x);
bool has_module_loaded(t_ntpython *x);
bool has_py_extention(char *scriptname);
bool is_compatible_value_type(PyObject *obj);

//////////////////////// global class pointer variable
void *ntpython_class;

void ext_main(void *r)
{
	t_class *c;

	c = class_new("nt.python", (method)ntpython_new, (method)ntpython_free, (long)sizeof(t_ntpython),
				  0L, A_GIMME, 0);

    class_addmethod(c, (method)ntpython_read,        "read",      A_DEFSYM, 0);
    class_addmethod(c, (method)ntpython_reload,      "reload",    A_NOTHING, 0);
    class_addmethod(c, (method)ntpython_anything,    "anything",  A_GIMME, 0);
    class_addmethod(c, (method)ntpython_bang,        "bang",      A_CANT, 0);

	class_register(CLASS_BOX, c);
	ntpython_class = c;
}


void load_python_script(t_ntpython *x, char *foldername, char *modulename){
    char syspath[MAX_PATH_CHARS];

    PyRun_SimpleString("import sys");
    sprintf(syspath, "sys.path.append(\"%s\")", foldername);
    PyRun_SimpleString(syspath);

    PyObject *pName = PyString_FromString(modulename);
    if (x->t_module != NULL) {
        Py_DECREF(x->t_module);
        if (x->t_modulename) free(x->t_modulename);
    } else {
        x->t_module = PyImport_Import(pName);
    }
    Py_DECREF(pName);

    if (x->t_module != NULL) {
        object_post((t_object *)x, "loaded: %s", modulename);
        x->t_modulename = strdup(modulename);
        outlet_bang(x->outlet2);
    }
    else {
        object_error((t_object *)x, "failed to load module: %s", modulename);
        print_python_error_message(x);
    }
}

PyObject *convert_to_python_object(t_atom *a){
    if (a->a_type == A_LONG) {
        return PyInt_FromLong(atom_getlong(a));
    } else if (a->a_type == A_FLOAT) {
        return PyFloat_FromDouble(atom_getfloat(a));
    } else if (a->a_type == A_SYM) {
        char *str = atom_getsym(a)->s_name;
        return PyString_FromString(str);
    }
    return NULL;
}

t_atom convert_to_max_object(PyObject *obj){
    t_atom a;
    if (PyInt_Check(obj)){
        atom_setlong(&a, PyInt_AsLong(obj));
        return a;
    }
    if (PyFloat_Check(obj)){
        atom_setfloat(&a, PyFloat_AsDouble(obj));
        return a;
    }
    if (PyString_Check(obj)){
        atom_setsym(&a, gensym(PyString_AsString(obj)));
        return a;
    }
    return a;
}

t_atomarray *convert_list_to_max_object(PyObject *obj){
    if (PyList_Check(obj)){
        PyObject *seq = PySequence_Fast(obj, "");
        Py_ssize_t len = PySequence_Size(obj);
        
        t_atomarray *array = atomarray_new(0, NULL);
        for (int i = 0; i < len; i++) {
            PyObject *item = PySequence_Fast_GET_ITEM(seq, i);
            t_atom a = convert_to_max_object(item);
            atomarray_appendatom(array, &a);
        }
        Py_DECREF(seq);
        return array;
    }
    return NULL;
}

void run_python_method(t_ntpython *x, t_symbol *s, long argc, t_atom *argv){
    if (!has_module_loaded(x)) return;
    
    char *func_name = s->s_name;
    
    PyObject *pFunc, *pArgs, *pValue;
    pFunc = PyObject_GetAttrString(x->t_module, func_name);
    /* pFunc is a new reference */
    
    long i;
    if (pFunc && PyCallable_Check(pFunc)) {
        pArgs = PyTuple_New(argc);
        for (i = 0; i < argc; ++i) {
            pValue = convert_to_python_object(argv + i);
            if (!pValue) {
                Py_DECREF(pArgs);
                object_error((t_object *)x, "cannot convert argument");
            }
            /* pValue reference stolen here: */
            PyTuple_SetItem(pArgs, i, pValue);
        }
        
        // Call
        pValue = PyObject_CallObject(pFunc, pArgs);
        Py_DECREF(pArgs);
        if (pValue != NULL) {
            if (is_compatible_value_type(pValue)){
                if (PyList_Check(pValue)){
                    t_atomarray *array = convert_list_to_max_object(pValue);
                    if (array){
                        outlet_list(x->outlet, NULL, array->ac, array->av);
                        object_free(array);
                    }
                } else {
                    t_atom a = convert_to_max_object(pValue);
                    outlet_atoms(x->outlet, 1, &a);
                }
                outlet_bang(x->outlet2); // Success
            } else { // not compatible
                object_error((t_object *)x, "got invalid object returned from function %s", func_name);
                object_error((t_object *)x, "(only compatible with int, flaot, string and list)");
            }
            Py_DECREF(pValue);
        }
        else {
            object_error((t_object *)x, "function call %s failed", func_name);
            print_python_error_message(x);
            Py_DECREF(pFunc);
        }
    } else{
        PyErr_Print();
        object_error((t_object *)x, "can't find function %s", func_name);
    }
}


void ntpython_assist(t_ntpython *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET)
		sprintf(s, "Message In");
}

void ntpython_read(t_ntpython *x, t_symbol *s)
{
	defer((t_object *)x, (method)ntpython_doread, s, 0, NULL);
}

void ntpython_doread(t_ntpython *x, t_symbol *s, long argc, t_atom *argv)
{
    char filename[MAX_PATH_CHARS], fullpath[MAX_PATH_CHARS];
    char foldername[MAX_PATH_CHARS], scriptname[MAX_PATH_CHARS];
    char modulename[100]; // 100-chars should be long enough for a filename??
    short path;
    t_fourcc type = FOUR_CHAR_CODE('TEXT');
    long err;
    
    bool found_script = false;
    
    if (s == gensym("")) {
        filename[0] = 0;
        if (open_dialog(filename, &path, &type, NULL, 0) == 0) {
            found_script = true;
        } else return; // not selected
    } else {
        strcpy(filename,s->s_name);
        if (locatefile_extended(filename,&path,&type,&type,1) == 0 ) {
            found_script = true;
        }
    }
    
    // If user specified a file or a file was found in File Paths
    if (found_script){
        err = path_toabsolutesystempath(path, filename, fullpath);
        if (!err) {
            path_splitnames(fullpath, foldername, scriptname);
            if (has_py_extention(scriptname)){
                strncpy(modulename, scriptname, strlen(scriptname) - 3);
                modulename[strlen(scriptname) - 3] = '\0';
            } else {
                strcpy(modulename, scriptname);
            }
            object_post((t_object*)x, "loading...: %s in %s", modulename, foldername);
            load_python_script(x, foldername, modulename);
        }
        return;
    }
    
    // Try to load a script with given name in the same directory as the patcher,
    // in which this object was created.
    if (!found_script){
        strcpy(filename,s->s_name);
        t_object *mypatcher;
        object_obex_lookup(x, gensym("#P"), &mypatcher);
        t_symbol *patcher_path = object_attr_getsym(mypatcher, gensym("filepath"));
        if (patcher_path != gensym("")){
            if (path_nameconform(patcher_path->s_name, fullpath, PATH_STYLE_SLASH, PATH_TYPE_BOOT)==0){
                char patcher_name[MAX_PATH_CHARS];
                path_splitnames(fullpath, foldername, patcher_name);
                if (has_py_extention(filename)){
                    strncpy(modulename, filename, strlen(filename) - 3);
                    modulename[strlen(scriptname) - 3] = '\0';
                } else {
                    strcpy(modulename, filename);
                }
                object_post((t_object*)x, "loading...: %s in %s", modulename, foldername);
                load_python_script(x, foldername, modulename);
            }
        } else {
            object_error((t_object *)x, "can't find file %s", filename);
        }
    }
}


void ntpython_reload(t_ntpython *x)
{
    defer((t_object *)x, (method)ntpython_doreload, NULL, 0, NULL);
}

void ntpython_doreload(t_ntpython *x, t_symbol *s, long argc, t_atom *argv)
{
    if (!has_module_loaded(x)) return;
    
    x->t_module = PyImport_ReloadModule(x->t_module);
    if (x->t_module){
        if (x->t_modulename) object_post((t_object *)x, "reloaded: %s", x->t_modulename);
        outlet_bang(x->outlet2);
    } else {
        print_python_error_message(x);
    }
}


void ntpython_bang(t_ntpython *x)
{
    outlet_bang(x->outlet);
}

void ntpython_anything(t_ntpython *x, t_symbol *s, long argc, t_atom *argv)
{
    run_python_method(x, s, argc, argv);
//    defer((t_object *)x, (method)run_python_method, s, argc, argv);
}

void *ntpython_new(t_symbol *s, long argc, t_atom *argv)
{
    Py_SetProgramName("nt.python");
    // Initializing Python Interpreter
    if (Py_IsInitialized() == 0) Py_Initialize();
    
	t_ntpython *x = NULL;
	x = (t_ntpython *)object_alloc(ntpython_class);
    
    // Credit
    object_post((t_object *)x, "ntpython %s - %s ", VERSION_STRING, CREDIT_STRING);
    
    // Arguments
    long i;
    for (i = 0; i < 1 && i < argc; i++) {
        if ((argv + i)->a_type == A_SYM) {
            char *scriptname = atom_getsym(argv+i)->s_name;
            if (!has_py_extention(scriptname)){
                char modulename[MAX_PATH_CHARS];
                sprintf(modulename, "%s%s", scriptname, ".py");
                ntpython_doread(x, gensym(modulename), argc, argv);
            } else {
                ntpython_doread(x, gensym(scriptname), argc, argv);
            }
        } else {
            object_error((t_object *)x, "invalid argument");
        }
    }
    
    x->outlet2 = bangout(x);
    x->outlet = outlet_new(x, NULL);
	return x;
}

void ntpython_free(t_ntpython *x)
{
    Py_XDECREF(x->t_module);
//  Py_Finalize();
    if (x->t_modulename) free(x->t_modulename);
}

#pragma mark UTILITY

bool has_module_loaded(t_ntpython *x){
    if (x->t_module == NULL) {
        object_error((t_object *)x, "load a Python script first!");
        return false;
    }
    return true;
}

// Current Limitation:
// compatible with int, float, string, list values in python only
bool is_compatible_value_type(PyObject *obj){
    return (PyInt_Check(obj)|| PyFloat_Check(obj)||PyString_Check(obj)||PyList_Check(obj));
}

// check if a filename has .py extention
bool has_py_extention(char *scriptname){
    return (strlen(scriptname) > 3 && !strcmp(scriptname + strlen(scriptname) - 3, ".py"));
}

void print_python_error_message(t_ntpython *x){
    // https://stackoverflow.com/questions/1796510/accessing-a-python-traceback-from-the-c-api
    PyObject *err = PyErr_Occurred(); // do not Py_DECREF()
    if (err != NULL) {
        PyObject *ptype, *pvalue, *ptraceback;
        PyObject *pystr, *module_name, *pyth_module, *pyth_func;
        
        PyErr_Fetch(&ptype, &pvalue, &ptraceback);
        pystr = PyObject_Str(pvalue);
        if (pystr){
            char *error_desc = PyString_AsString(pystr); // do not free string
            if (error_desc != NULL) {
                object_error((t_object *)x, error_desc);
            }
            Py_XDECREF(pystr);
        }
        
        /* See if we can get a full traceback */
        module_name = PyString_FromString("traceback");
        pyth_module = PyImport_Import(module_name);
        Py_DECREF(module_name);
        if (pyth_module == NULL) return;
        
        pyth_func = PyObject_GetAttrString(pyth_module, "format_exception");
        if (pyth_func && PyCallable_Check(pyth_func)) {
            PyObject *pyth_val;
            
            pyth_val = PyObject_CallFunctionObjArgs(pyth_func, ptype, pvalue, ptraceback, NULL);
            if (pyth_val){
                pystr = PyObject_Str(pyth_val);
                if (pystr){
                    char *full_backtrace = PyString_AsString(pystr);
                    if (full_backtrace != NULL) object_error((t_object *)x, full_backtrace);
                    Py_XDECREF(pystr);
                }
                Py_XDECREF(pyth_val);
            }
            Py_DECREF(pyth_func);
        }
        Py_XDECREF(pyth_module);
    }
}
