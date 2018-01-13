/**
	@file
	nt.python - run python script on max
    Nao Tokui  www.naotokui.net
 
	@ingroup	
*/

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object

#include <Python/Python.h>

////////////////////////// object struct
typedef struct _ntpython
{
	t_object ob;			// the object itself (must be first)
	t_object *t_editor;
    void     *outlet;
	char    **t_text;
	long t_size;
    
    PyObject *t_module;
} t_ntpython;

///////////////////////// function prototypes
//// standard set
void *ntpython_new(t_symbol *s, long argc, t_atom *argv);
void ntpython_free(t_ntpython *x);
void ntpython_assist(t_ntpython *x, void *b, long m, long a, char *s);
void ntpython_read(t_ntpython *x, t_symbol *s);
void ntpython_doread(t_ntpython *x, t_symbol *s, long argc, t_atom *argv);
void ntpython_anything(t_ntpython *x, t_symbol *s, long argc, t_atom *argv);
//void ntpython_dblclick(t_ntpython *x);
//void ntpython_edclose(t_ntpython *x, char **text, long size);
void ntpython_bang(t_ntpython *x);
//////////////////////// global class pointer variable
void *ntpython_class;


void ext_main(void *r)
{
	t_class *c;

	c = class_new("ntpython", (method)ntpython_new, (method)ntpython_free, (long)sizeof(t_ntpython),
				  0L, A_GIMME, 0);

    class_addmethod(c, (method)ntpython_read,            "read",        A_DEFSYM, 0);
//    class_addmethod(c, (method)ntpython_dblclick,        "dblclick",    A_CANT, 0);
//    class_addmethod(c, (method)ntpython_edclose,        "edclose",    A_CANT, 0);
    class_addmethod(c, (method)ntpython_anything,       "anything",        A_GIMME, 0);
    class_addmethod(c, (method)ntpython_bang,        "bang",    A_CANT, 0);

	class_register(CLASS_BOX, c);
	ntpython_class = c;
}

void load_python_script(t_ntpython *x, char *foldername, char *modulename){
    char syspath[MAX_PATH_CHARS];

    PyRun_SimpleString("import sys");
    sprintf(syspath, "sys.path.append(\"%s\")", foldername);
    PyRun_SimpleString(syspath);

    PyObject *pName;
    pName = PyString_FromString(modulename);
    /* Error checking of pName left out */

    x->t_module = PyImport_Import(pName);
    Py_DECREF(pName);

    if (x->t_module != NULL) {
        object_post((t_object *)x, "loaded: %s", modulename);
    }
    else {
        PyErr_Print();
        object_error((t_object *)x, "failed to load file %s", modulename);
    }
}


void print_python_error_message(t_ntpython *x){
    // https://stackoverflow.com/questions/1796510/accessing-a-python-traceback-from-the-c-api
    PyObject *err = PyErr_Occurred();
    if (err != NULL) {
        PyObject *ptype, *pvalue, *ptraceback;
        PyObject *pystr, *module_name, *pyth_module, *pyth_func;
        char *str, *full_backtrace;
        
        PyErr_Fetch(&ptype, &pvalue, &ptraceback);
        pystr = PyObject_Str(pvalue);
        str = PyString_AsString(pystr);
        char *error_desc = strdup(str);
        if (error_desc != NULL) {
            object_error((t_object *)x, error_desc);
            free(error_desc);
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
            
            pystr = PyObject_Str(pyth_val);
            str = PyString_AsString(pystr);
            full_backtrace = strdup(str);
            if (full_backtrace != NULL) object_error((t_object *)x, full_backtrace);
            if (full_backtrace != NULL) free(full_backtrace);
            Py_DECREF(pyth_val);
            Py_DECREF(pyth_func);
        }
        Py_DECREF(pyth_module);
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
        //        atom_set(&a, PyString_AsString(obj));
        return a;
    }
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
}



void run_python_method(t_ntpython *x, t_symbol *s, long argc, t_atom *argv){
    if (x->t_module == NULL) {
        PyErr_Print();
        object_error((t_object *)x, "load a Python script first!");
        return;
    }
    
    char *func_name = s->s_name;
    object_post((t_object *)x, "function (%s)", func_name);
    
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

            if (PyList_Check(pValue)){
                t_atomarray *array = convert_list_to_max_object(pValue);
                outlet_list(x->outlet, NULL, array->ac, array->av);
                object_free(array);
            } else {
                t_atom a = convert_to_max_object(pValue);
                outlet_atoms(x->outlet, 1, &a);
            }
            
            printf("Result of call: %ld\n", PyInt_AsLong(pValue));
            object_error((t_object *)x, "Result of call: %ld\n", PyInt_AsLong(pValue));
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

void ntpython_free(t_ntpython *x)
{
	if (x->t_text)
		sysmem_freehandle(x->t_text);
    
    Py_DECREF(x->t_module);
    Py_Finalize();
}

void ntpython_read(t_ntpython *x, t_symbol *s)
{
	defer((t_object *)x, (method)ntpython_doread, s, 0, NULL);
}

void ntpython_bang(t_ntpython *x)
{
    outlet_bang(x->outlet);
}

void ntpython_anything(t_ntpython *x, t_symbol *s, long argc, t_atom *argv)
{
    defer((t_object *)x, (method)run_python_method, s, argc, argv);
}

void ntpython_doread(t_ntpython *x, t_symbol *s, long argc, t_atom *argv)
{
	char filename[MAX_PATH_CHARS], fullpath[MAX_PATH_CHARS];
    char foldername[MAX_PATH_CHARS], scriptname[MAX_PATH_CHARS];
    char modulename[100]; // 100-chars should be long enough for a filename
	short path;
	t_fourcc type = FOUR_CHAR_CODE('TEXT');
	long err;
	t_filehandle fh;

	if (s == gensym("")) {
		filename[0] = 0;
		if (open_dialog(filename, &path, &type,  &type, 1))
			return;
	} else {
		strcpy(filename,s->s_name);
		if (locatefile_extended(filename,&path,&type,&type,1)) {
			object_error((t_object *)x, "can't find file %s",filename);
			return;
		}
	}
// success
    
    err = path_toabsolutesystempath(path, filename, fullpath);
	if (!err) {
        object_post((t_object *)x, "fullpath (%s)",fullpath);
        path_splitnames(fullpath, foldername, scriptname);
        
        object_post((t_object *)x, "foldername (%s)",foldername);
        
        if (strlen(scriptname) > 3 && !strcmp(scriptname + strlen(scriptname) - 3, ".py")){
            strncpy(modulename, scriptname, strlen(scriptname) - 3);
        }
        object_post((t_object *)x, "modulename (%s)",modulename);
        
        load_python_script(x, foldername, modulename);
        
//        sysfile_readtextfile(fh, x->t_text, 0, TEXT_LB_UNIX | TEXT_NULL_TERMINATE);
//        sysfile_close(fh);
//        x->t_size = sysmem_handlesize(x->t_text);
	}
}

//void ntpython_dblclick(t_ntpython *x)
//{
//    if (x->t_editor)
//        object_attr_setchar(x->t_editor, gensym("visible"), 1);
//    else {
//        x->t_editor = object_new(CLASS_NOBOX, gensym("jed"), x, 0);
//        object_method(x->t_editor, gensym("settext"), *x->t_text, gensym("utf-8"));
//        object_attr_setchar(x->t_editor, gensym("scratch"), 1);
//        object_attr_setsym(x->t_editor, gensym("title"), gensym("ntpython"));
//    }
//}

//void ntpython_edclose(t_ntpython *x, char **text, long size)
//{
//    if (x->t_text)
//        sysmem_freehandle(x->t_text);
//
//    x->t_text = sysmem_newhandleclear(size+1);
//    sysmem_copyptr((char *)*text, *x->t_text, size);
//    x->t_size = size+1;
//    x->t_editor = NULL;
//}

void *ntpython_new(t_symbol *s, long argc, t_atom *argv)
{
    Py_Initialize();
    
	t_ntpython *x = NULL;

	x = (t_ntpython *)object_alloc(ntpython_class);
    
    object_post((t_object *)x, "a new %s object was instantiated: %p", s->s_name, x);
    object_post((t_object *)x, "it has %ld arguments", argc);
    
    long i;
    for (i = 0; i < argc; i++) {
        if ((argv + i)->a_type == A_LONG) {
            object_post((t_object *)x, "arg %ld: long (%ld)", i, atom_getlong(argv+i));
        } else if ((argv + i)->a_type == A_FLOAT) {
            object_post((t_object *)x, "arg %ld: float (%f)", i, atom_getfloat(argv+i));
        } else if ((argv + i)->a_type == A_SYM) {
            char *script_name = atom_getsym(argv+i)->s_name;
            object_post((t_object *)x, "arg %ld: symbol (%s)", i, script_name);
        //    load_python_script(x, script_name);
        } else {
            object_error((t_object *)x, "forbidden argument");
        }
    }
    
    x->outlet = outlet_new(x, NULL);
	x->t_text = sysmem_newhandle(0);
	x->t_size = 0;
	x->t_editor = NULL;
    
	return x;
}
