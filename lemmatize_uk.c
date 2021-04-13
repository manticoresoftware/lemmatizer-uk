// Copyright (c) 2020-2021, Manticore Software LTD (https://manticoresearch.com)
// All rights reserved
//
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifdef _DEBUG
#define _DEBUG_WAS_DEFINED
#undef _DEBUG
#endif

#include "Python.h"

#ifdef _DEBUG_WAS_DEFINED
#define _DEBUG
#undef _DEBUG_WAS_DEFINED
#endif

#include "sphinxudf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// UDF logging callback
/// write any message into daemon's log.
sphinx_log_fn * sphlog = NULL;

/// UDF set logging callback
/// gets called once when the library is loaded; daemon set callback function in this call
DLLEXPORT void lemmatize_uk_setlogcb ( sphinx_log_fn* cblog )
{
	sphlog = cblog;
}

void UdfLog ( char * szMsg )
{
	if ( sphlog )
		( *sphlog ) ( szMsg, -1 );
}

#define MAX_TOKEN_LEN 256

DLLEXPORT int lemmatize_uk_ver ()
{
	return SPH_UDF_VERSION;
}

typedef struct module_data
{
    PyObject *  module;
    PyObject *  morph;
    PyThreadState * thd_save;
    int python_initialized;
} MODULEDATA;

MODULEDATA * module_data;

int reject_version ( const char * py_version )
{
    // from python docs
    // the first three characters are the major and minor version separated by a period

    int py_major;
    int py_minor;
    int got_num = sscanf ( py_version, "%d.%d", &py_major, &py_minor );
    if ( got_num<2 )
        return 1;

    // reject for 2.X
    if ( py_major<3 )
        return 1;

    // pass any after 3.X
    if ( py_major>3 )
        return 0;

    // pass any after 3.9
    if ( py_minor>=9 )
        return 0;

    // reject 3.[0-8]
    return 1;
}

DLLEXPORT int plugin_load ( char * error_message )
{
    module_data = malloc ( sizeof(MODULEDATA) );
    if ( module_data==0 )
    {
        snprintf ( error_message, SPH_UDF_ERROR_LEN, "malloc() failed" );
        return 1;
    }
    memset ( module_data, 0, sizeof ( *module_data ) );

    // only 1st call initialize Python
    if ( Py_IsInitialized()==0 )
    {
        Py_Initialize();
        if ( Py_IsInitialized()==0 )
        {
            snprintf ( error_message, SPH_UDF_ERROR_LEN, "python initialize failed" );
            return 1;
        }

        module_data->python_initialized = 1;

        const char * py_version = Py_GetVersion();
        if ( !py_version )
        {
            snprintf ( error_message, SPH_UDF_ERROR_LEN, "invalid python version at least 3.9 required" );
            return 1;
        }

        // reject for 2.X or <3.9
        if ( reject_version ( py_version ) )
        {
            snprintf ( error_message, SPH_UDF_ERROR_LEN, "invalid python version '%s' at least 3.9 required", py_version );
            return 1;
        }
    }

    PyObject *  module = PyImport_ImportModule ( "pymorphy2" );
    module_data->module = module;
    if ( !module_data->module )
    {
        PyErr_Print(); // !COMMIT
        snprintf ( error_message, SPH_UDF_ERROR_LEN, "python failed to import module pymorphy2" );
        return 1;
    }
    Py_INCREF ( module_data->module );

    PyObject * fn = PyObject_GetAttrString ( module_data->module, "MorphAnalyzer" );
    PyObject * fn_args = PyTuple_New ( 0 );
    PyObject * fn_kwargs = Py_BuildValue ( "{s:s}", "lang", "uk" );
    PyObject * morph = PyObject_Call ( fn, fn_args, fn_kwargs );
    if ( !morph )
    {
        //PyErr_Print(); // FIXME!!! replace with PyErr_Fetch + PyErr_NormalizeException
        snprintf ( error_message, SPH_UDF_ERROR_LEN, "failed to create MorphAnalyzer from pymorphy2" );
        return 1;
    }

    Py_DECREF ( fn_kwargs );
    Py_DECREF ( fn_args );
    Py_DECREF ( fn );

    PyObject * method_name = Py_BuildValue ( "s", "parse" );
    module_data->morph = PyObject_GetAttr ( morph, method_name );
    if ( !module_data->morph )
    {
        //PyErr_Print(); // FIXME!!! replace with PyErr_Fetch + PyErr_NormalizeException
        snprintf ( error_message, SPH_UDF_ERROR_LEN, "failed to create MorphAnalyzer from pymorphy2" );
        return 1;
    }

    Py_DECREF ( morph );
    Py_DECREF ( method_name );

    // release GIL for other threads
    module_data->thd_save = PyEval_SaveThread();

    return 0;
}

DLLEXPORT int plugin_unload ( char * error_message )
{
    // release state
	if ( module_data )
	{
        PyEval_RestoreThread ( module_data->thd_save );

        Py_CLEAR ( module_data->morph );
        Py_CLEAR ( module_data->module );

        // only plugin that initilize Python shutdown it
        if ( module_data->python_initialized )
            Py_Finalize();

		free ( module_data );
        module_data = 0;
	}

    return 0;
}

typedef struct token_data
{
	char		result_token[MAX_TOKEN_LEN];
    PyObject *  terms;
    int         cur_token;
    int         tokens_count;

    // stats counters
    int         src_tokens;
    int         dst_tokens;

} TOKENDATA;

int plugin_init ( void ** userdata, char * error_message )
{
	TOKENDATA * data = malloc(sizeof(TOKENDATA));
    if ( data==0 )
    {
        snprintf ( error_message, SPH_UDF_ERROR_LEN, "malloc() failed" );
        return 1;
    }
	memset ( data, 0, sizeof(*data) );
	*userdata=data;

	return 0;
}

void plugin_deinit ( void * userdata )
{
    TOKENDATA * data = (TOKENDATA*)userdata;
    
    if ( data )
    {
        // dump stats on lemmatizer instance done
        //printf ( "\ntokens %d(%d)\n", data->src_tokens, data->dst_tokens );

        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();

        Py_CLEAR ( data->terms );

        PyGILState_Release(gstate);

	    free ( userdata );
    }
}

/// returns 0 on success, MUST fill error_message otherwise
DLLEXPORT int luk_init ( void ** userdata, int num_fields, const char ** field_names, const char * options, char * error_message )
{
    return plugin_init ( userdata, error_message );
}

/// final cleanup
DLLEXPORT void luk_deinit ( void * userdata )
{
    plugin_deinit ( userdata );
}

int parse_morph_result ( PyObject * res, TOKENDATA * data )
{
    if ( !res )
    {
        //PyErr_Print(); // FIXME!!! replace with PyErr_Fetch + PyErr_NormalizeException
        UdfLog ( "empty result token" );
        return 0;
    }

    if ( !PyList_Check ( res ) )
    {
        UdfLog ( "result token is not list" );
        return 0;
    }

    // already own res from PyObject_CallOneArg -  no need to Py_INCREF
    data->terms = res;
    data->cur_token = 0;
    data->tokens_count = (int)PyList_Size ( res );

    return 1;
}

int parse_token ( TOKENDATA * data, char * token )
{
    Py_CLEAR ( data->terms ); // dec ref previous item
    data->cur_token = 0;
    data->tokens_count = 0;
    data->src_tokens++;

    PyObject * morph_token = Py_BuildValue ( "s", token );

    PyObject * res = PyObject_CallOneArg( module_data->morph, morph_token );
    int iOk = parse_morph_result ( res, data );

    Py_CLEAR ( morph_token );

    return iOk;
}

int get_token ( TOKENDATA * data )
{
    if ( data->cur_token>=data->tokens_count )
        return 0;

    PyObject * form = PyList_GetItem ( data->terms, data->cur_token );
    PyObject * token = PyObject_GetAttrString ( form, "normal_form" );

    data->cur_token++;
    data->dst_tokens++;

    const char * res_str = PyUnicode_AsUTF8 ( token );
    strncpy ( data->result_token, res_str, MAX_TOKEN_LEN );

    Py_CLEAR ( token );

    return 1;
}

DLLEXPORT char * luk_push_token ( void * userdata, char * token, int * extra, int * delta )
{
	TOKENDATA * data = (TOKENDATA*)userdata;
    *extra = 0;

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    if ( !parse_token ( data, token ) )
    {
        PyGILState_Release(gstate);
        return token;
    }

    if ( data->tokens_count>1 )
        *extra = data->tokens_count - 1;
    get_token ( data );

    PyGILState_Release(gstate);

	return data->result_token;
}

DLLEXPORT char * luk_get_extra_token ( void * userdata, int * delta )
{
    TOKENDATA * data = (TOKENDATA*)userdata;

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    int iGotToken = get_token ( data );

    PyGILState_Release(gstate);

    if ( iGotToken )
    {
        *delta = 0;
        return data->result_token;
    }

	return NULL;
}
