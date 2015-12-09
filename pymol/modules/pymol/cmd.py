
#A* -------------------------------------------------------------------
#B* This file contains source code for the PyMOL computer program
#C* Copyright (c) Schrodinger, LLC. 
#D* -------------------------------------------------------------------
#E* It is unlawful to modify or remove this copyright notice.
#F* -------------------------------------------------------------------
#G* Please see the accompanying LICENSE file for further information. 
#H* -------------------------------------------------------------------
#I* Additional authors of this source file include:
#-* 
#-* 
#-*
#Z* -------------------------------------------------------------------

# cmd.py 
# Python interface module for PyMol
#
# **This is the only module which should be/need be imported by 
# ** PyMol API Based Programs

# NEW CALL RETURN CONVENTIONS for _cmd.so C-layer
#

# (1) Calls into C (_cmd) should return results/status and print
#     errors and feedback (according to mask) BUT NEVER RAISE EXCEPTIONS
#     from within the C code itself.

# (2) Effective with version 0.99, standard Python return conventions
# apply, but haven't yet been fully implemented.  In summary:

#     Unless explicitly specified in the function:

#     ==> Success with no information should return None

#     ==> Failure should return a negative number as follows:
#        -1 = a general, unspecified failure
          
#     Upon an error, exceptions will be raised by the Python wrapper
#     layer if the "raise_exceptions" setting is on.

#     ==> Boolean queries should return 1 for true/yes and 0 for false/no.

#     ==> Count queries should return 0 or a positive number

# (3) If _cmd produces a specific return result, be sure to include an
#     error result as one of the possibilities outside the range of the
#     expected return value.  For example, a negative distance
#
# (4) cmd.py API wrappers can then raise exceptions and return values.
#
# NOTE: Output tweaking via the "quiet" parameter of API functions.
#
# Many PyMOL API functions have a "quiet" parameter which is used to
# customize output depending on when and where the routine was called.
#
# As defined, quiet should be 1.  Called from an external module, output
# to the console should be minimal.
#
# However, when a command is run through the parser (often manually) the
# user expects a little more feedback.  The parser will automatically
# set "quiet" to zero
#
# In rare cases, certain nonserious error or warning output should
# also be suppressed.  Set "quiet" to 2 for this behavior.

from __future__ import print_function

def _deferred_init_pymol_internals(_pymol):
    # set up some global session tasks

    if viewing.session_restore_views not in _pymol._session_restore_tasks:
        _pymol._session_restore_tasks.append(viewing.session_restore_views)

    if viewing.session_save_views not in _pymol._session_save_tasks:
        _pymol._session_save_tasks.append(viewing.session_save_views)

    if viewing.session_restore_scenes not in _pymol._session_restore_tasks:
        _pymol._session_restore_tasks.append(viewing.session_restore_scenes)

    if wizarding.session_restore_wizard not in _pymol._session_restore_tasks:
        _pymol._session_restore_tasks.append(wizarding.session_restore_wizard)

    if wizarding.session_save_wizard not in _pymol._session_save_tasks:
        _pymol._session_save_tasks.append(wizarding.session_save_wizard)

    # take care of some deferred initialization

    _pymol._view_dict_sc = Shortcut({})
    _pymol._scene_dict_sc = Shortcut({})

    # 
if __name__=='pymol.cmd':

    import traceback
    import sys

    try:
        
        import re
        from pymol import _cmd
        import string
        import threading
        import pymol
        import os
        from . import parsing
        import time

        _pymol = pymol
        
        from .shortcut import Shortcut

        from chempy import io

        #######################################################################
        # symbols for early export
        #######################################################################

        from .constants import *
        from .constants import _load2str

        fb_debug = sys.stderr # can redirect python debugging output elsewhere if desred...

        #--------------------------------------------------------------------
        # convenient type and result checking

        from .checking import *
        from .checking import _raising
        
        #-------------------------------------------------------------------
        # path expansion, including our fixes for Win32

        def _nt_expandvars(path): # allow for //share/folder$/file
            path = nt_hidden_path_re.sub(r"$$\\",path)
            return os.path.expandvars(path)
        
        if "nt" in sys.builtin_module_names:
            _expandvars = _nt_expandvars
        else:
            _expandvars = os.path.expandvars
        
        def exp_path(path):
            return _expandvars(os.path.expanduser(path))
        
        #--------------------------------------------------------------------
        # locks and threading

        reaper = None

        # the following locks are used by both C and Python to insure that no more than
        # one active thread enters PyMOL at a given time. 
        
        lock_api = pymol.lock_api
        lock_api_c = pymol.lock_api_c
        lock_api_status = pymol.lock_api_status
        lock_api_glut = pymol.lock_api_glut
        lock_api_data = pymol.lock_api_data
        lock_api_allow_flush = 1
        
        from .locking import *
        lockcm = LockCM()

        #--------------------------------------------------------------------
        # status monitoring
        
        from .monitoring import *
        
        #--------------------------------------------------------------------
        # Feedback

        from .feedingback import *
        from .feedingback import _feedback

        #--------------------------------------------------------------------
        # internal API routines

        from . import internal

        _adjust_coord = internal._adjust_coord
        _alt = internal._alt
        _coordset_update_spawn = internal._coordset_update_spawn
        _coordset_update_thread = internal._coordset_update_thread
        _copy_image = internal._copy_image
        _ctrl = internal._ctrl
        _ctsh = internal._ctsh
        _do = internal._do
        _dump_floats = internal._dump_floats
        _dump_ufloats = internal._dump_ufloats
        _fake_drag = internal._fake_drag
        _get_color_sc = internal._get_color_sc
        _get_feedback = internal._get_feedback
        _interpret_color = internal._interpret_color
        _invalidate_color_sc = internal._invalidate_color_sc
        _load = internal._load
        _mpng = internal._mpng
        _object_update_spawn = internal._object_update_spawn
        _object_update_thread = internal._object_update_thread
        _png = internal._png
        _quit = internal._quit
        _ray_anti_spawn = internal._ray_anti_spawn
        _ray_hash_spawn = internal._ray_hash_spawn
        _ray_spawn = internal._ray_spawn
        _refresh = internal._refresh
        _sgi_stereo = internal._sgi_stereo
        _special = internal._special
        _validate_color_sc = internal._validate_color_sc
        _cache_get = internal._cache_get
        _cache_set = internal._cache_set
        _cache_clear = internal._cache_clear
        _cache_purge = internal._cache_purge
        _cache_mark = internal._cache_mark
        _sdof = internal._sdof
        
        # when adding, remember to also edit cmd2.py

        get_feedback = _get_feedback # legacy
        
        #######################################################################
        # now import modules which depend on the above
        #######################################################################

        from . import editor

        #######################################################################
        # cmd module functions...
        #######################################################################
            
        # for extending the language

        from .commanding import extend, extendaa, alias

        # for documentation etc

        from .helping import python_help
                
        def write_html_ref(file):
            '''Write the PyMOL Command Reference to an HTML file'''
            f=open(file,'w')
            kees = [a for a in keywords.get_command_keywords()
                    if not a.startswith('_') and keyword[a][0] != python_help]
            kees.sort()
            title = 'PyMOL Command Reference'
            f.write('''<html>
<head>
<title>%s</title>
<style type='text/css'>
body, p, h1, h2 {
  font-family: sans-serif;
}
p.api {
  font:small monospace;
  color:#999;
}
pre.example {
  background-color: #ccc;
  padding: 5px;
}
li {
  display: block;
  width: 10em;
  float: left;
}
</style>
</head>
<body>
<h1>%s</h1>

<p>This is the list of all PyMOL commands which can be used in the PyMOL
command line and in PML scripts. The command descriptions found in this
file can also be printed to the PyMOL text buffer with the
<a href="#help">help</a> command. Example:</p>

<pre class="example">PyMOL&gt;help color
...</pre>

<p>The list of arguments for a command (the "usage") can be queried on
the command line with a questionmark. Example:</p>

<pre class="example">PyMOL&gt;color ?
Usage: color color [, selection [, quiet [, flags ]]]</pre>

<p>The square brackets ("[" and "]") indicate optional arguments and are
not part of the syntax.</p>

<p>If the PyMOL command interpreter doesn't understand some input, it passes
it to the Python interpreter. This means that single-line Python expressions
can be put into PML scripts or typed into the command line. Prefixing a line
with a slash (/) forces the interpreter to pass it to Python. See also the
<a href="#python">python</a> command to input multi-line Python scripts.</p>

<p>This file can be generated on the PyMOL command line:</p>
<pre class="example">PyMOL&gt;cmd.write_html_ref('pymol-command-ref.html')</pre>

<hr size=1>

<ul>
''' % (title, title))

            for a in kees:
                f.write("<li><a href='#%s'>%s</a></li>" % (a, a))

            f.write('</ul><br style="clear: both">')

            def make_see_also_link(m):
                w = m.group()
                if w in kees:
                    return "<a href='#%s'>%s</a>" % (w, w)
                return w

            for a in kees:
                func = keyword[a][0]
                doc = (getattr(func, '__doc__') or 'UNDOCUMENTED').strip(). \
                        replace("<", "&lt;"). \
                        replace(">", "&gt;").splitlines()

                # attemt to do some HTML formatting
                isseealso = False
                for i, line in enumerate(doc):
                    if not line.strip():
                        continue
                    isindented = line[:1].isspace()
                    if isindented:
                        if isseealso:
                            doc[i] = re.sub(r'\w+', make_see_also_link, line)
                    elif line.isupper():
                        isseealso = line.startswith('SEE ALSO')
                        doc[i] = '<b>' + line + '</b>'

                f.write("<hr size=1><h2 id='%s'>%s</h2>" % (a, a))
                f.write("<pre>%s</pre>" % ('\n'.join(doc)))
                f.write("<p class='api'>api: %s.%s</p>" % (func.__module__, func.__name__))
            f.write("</BODY></HTML>")
            f.close()

            print("PyMOL Command Reference written to %s" % (os.path.abspath(file)))

        
        #####################################################################
        # Here is where the PyMOL Command Language and API are built.
        #####################################################################

        # first we need to import a set of symbols into the local namespace

        from .api import *

        # deferred initialization

        _deferred_init_pymol_internals(pymol)
        
        # now we create the command langauge
        
        from . import keywords
        keyword = keywords.get_command_keywords()
        kw_list = list(keyword.keys())
        
        keywords.fix_list(kw_list)
        kwhash = Shortcut(kw_list)
        keywords.fix_dict(keyword)
        
        # informational or API-only functions which don't exist in the
        # PyMOL command language namespace

        help_only = keywords.get_help_only_keywords()
        help_sc = Shortcut(list(keyword.keys())+list(help_only.keys()))

        # keyboard configuration
        
        from . import keyboard
        
        special = keyboard.get_special()

        shft_special = keyboard.get_shft_special()        
        alt_special = keyboard.get_alt_special()        
        ctrl_special = keyboard.get_ctrl_special()
        ctsh_special = keyboard.get_ctsh_special()

        ctrl = keyboard.get_ctrl()        
        alt = keyboard.get_alt()
        ctsh = keyboard.get_ctsh()

        selection_sc = lambda sc=Shortcut,gn=get_names:sc(gn('public')+['all'])
        object_sc = lambda sc=Shortcut,gn=get_names:sc(gn('objects'))
        map_sc = lambda sc=Shortcut,gnot=get_names_of_type:sc(gnot('object:map'))
        contour_sc =  lambda sc=Shortcut,gnot=get_names_of_type:sc(gnot('object:mesh')+gnot('object:surface'))
        group_sc = lambda sc=Shortcut,gnot=get_names_of_type:sc(gnot('object:group'))
        
        # Table for argument autocompletion

        from . import completing
        
        auto_arg = completing.get_auto_arg_list()

        color_sc = None

    except:
        print("Error: unable to initalize the pymol.cmd module")
        traceback.print_exc()
        sys.exit(0)
        
else:
    from pymol.cmd import *
    

