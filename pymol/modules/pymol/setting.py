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

from __future__ import print_function

if __name__=='pymol.setting':
    
    import traceback
    import string
    import types
    from . import selector
    from .shortcut import Shortcut
    cmd = __import__("sys").modules["pymol.cmd"]
    from .cmd import _cmd,lock,lock_attempt,unlock,QuietException, \
          is_string, \
          _feedback,fb_module,fb_mask, \
          DEFAULT_ERROR, DEFAULT_SUCCESS, _raising, is_ok, is_error        
    import re
    
    boolean_type = 1
    int_type     = 2
    float_type   = 3
    float3_type  = 4

    self = cmd

    # name -> index mapping
    index_dict = _cmd.get_setting_indices()

    # index -> name mapping
    name_dict = dict((v,k) for (k,v) in index_dict.items())

    name_list = list(index_dict.keys())
    setting_sc = Shortcut(name_list)

    # legacy
    index_dict['ray_shadows'] =     index_dict['ray_shadow']

    # legacy, in case someone used that in a script
    class SettingIndex:
        def __getattr__(self, name):
            return index_dict[name]

    boolean_dict = {
        "true" : 1,
        "false": 0,
        "on"   : 1,
        "off"  : 0,
        "1"    : 1,
        "0"    : 0,
        "1.0"  : 1,
        "0.0"  : 0,
        }

    boolean_sc = Shortcut(boolean_dict.keys())

    quote_strip_re = re.compile("^\'.*\'$|^\".*\"$")

    def _get_index(name):
        '''Get setting index for given name. `name` may be abbreviated.
        Raises QuietException for unknown names or ambiguous abbreviations.'''
        if isinstance(name, int) or name.isdigit():
            return int(name)
        if name not in index_dict:
            name = setting_sc.auto_err(name, 'Setting')
        return index_dict[name]

    def _get_name(index):
        # legacy, in case someone used that in a script
        return name_dict.get(index, "")

    def get_index_list():
        # legacy, in case someone used that in a script (e.g. grepset)
        return list(name_dict.keys())

    def get_name_list():
        return name_list

    ###### API functions

    def set_bond(name, value, selection1, selection2=None,
                 state=0, updates=1, log=0, quiet=1, _self=cmd):
        ''' 
DESCRIPTION

    "set_bond" changes per-bond settings for all bonds which exist
    between two selections of atoms.

USAGE

    set_bond name, value, selection1 [, selection2 ]

ARGUMENTS

    name = string: name of the setting

    value = string: new value to use

    selection1 = string: first set of atoms

    selection2 = string: seconds set of atoms {default: (selection1)}

EXAMPLE

    set_bond stick_transparency, 0.7, */n+c+ca+o


NOTES

    The following per-bond settings are currently implemented.  Others
    may seem to be recognized but will currently have no effect when
    set at the per-bond level.
    
    * valence
    * line_width
    * line_color
    * stick_radius
    * stick_color
    * stick_transparency

    Note that if you attempt to use the "set" command with a per-bond
    setting over a selection of atoms, the setting change will appear
    to take, but no change will be observed.
    
PYMOL API

    cmd.set_bond ( string name, string value,
                   string selection1,
                   string selection2,
                   int state, int updates, log=0, quiet=1)

       '''
        r = DEFAULT_ERROR
        selection1 = str(selection1)
        if selection2 == None:
            selection2 = selection1
        index = _get_index(str(name))
        if log:
            name = name_dict.get(index, name)
            _self.log('', "cmd.set_bond('%s',%s,%s,%s,%s)\n" % (name, repr(value),
                repr(selection1), repr(selection2), state))
        if True:
            try:
                _self.lock(_self)
                type = _cmd.get_setting_tuple(_self._COb,int(index),str(""),int(-1))[0]
                if type==None:
                    print("Error: unable to get setting type.")
                    raise QuietException
                try:
                    if type==1: # boolean (also support non-zero float for truth)
                        handled = 0
                        if boolean_sc.interpret(str(value))==None:
                            try: # number, non-zero, then interpret as TRUE
                                if not (float(value)==0.0):
                                    handled = 1
                                    v = (1,)
                                else:
                                    handled = 1
                                    v = (0,)
                            except:
                                pass
                        if not handled:
                            v = (boolean_dict[
                                boolean_sc.auto_err(
                                str(value),"boolean")],)
                    elif type==2: # int (also supports boolean language for 0,1)
                        if boolean_sc.has_key(str(value)):
                            v = (boolean_dict[
                                boolean_sc.auto_err(
                                str(value),"boolean")],)
                        else:
                            v = (int(value),)
                    elif type==3: # float
                        if boolean_sc.has_key(str(value)):
                            v = (float(boolean_dict[
                                boolean_sc.auto_err(
                                str(value),"boolean")]),)
                        else:
                            v = (float(value),)
                    elif type==4: # float3 - some legacy handling req.
                        if is_string(value):
                            if not ',' in value:
                                v = string.split(value)
                            else:
                                v = eval(value)
                        else:
                            v = value
                        v = (float(v[0]),float(v[1]),float(v[2]))
                    elif type==5: # color
                        v = (str(value),)
                    elif type==6: # string
                        vl = str(value)
                        # strip outermost quotes (cheesy approach)
                        if quote_strip_re.search(vl)!=None:
                            vl=vl[1:-1]
                        v = (vl,)
                    v = (type,v)
                    if len(selection1):
                        selection1=selector.process(selection1)
                    if len(selection2):
                        selection2=selector.process(selection2)                        
                    r = _cmd.set_bond(_self._COb,int(index),v,
                                 "("+selection1+")","("+selection2+")",
                                 int(state)-1,int(quiet),
                                 int(updates))
                except QuietException:
                    pass
                except:
                    if(_feedback(fb_module.cmd,fb_mask.debugging,_self)):
                        traceback.print_exc()
                    raise _self.pymol.CmdException("invalid value: %s" % repr(value))
            finally:
                _self.unlock(r,_self)
        if _self._raising(r,_self): raise QuietException            
        return r

        
    def set(name, value=1, selection='', state=0, updates=1, log=0,
            quiet=1,_self=cmd):
        
        '''
DESCRIPTION

    "set" changes global, object, object-state, or per-atom settings.

USAGE

    set name [,value [,selection [,state ]]]

ARGUMENTS

    name = string: setting name

    value = string: a setting value {default: 1}

    selection = string: name-pattern or selection-expression
    {default:'' (global)}

    state = a state number {default: 0 (per-object setting)}

EXAMPLES

    set orthoscopic

    set line_width, 3

    set surface_color, white, 1hpv
    
    set sphere_scale, 0.5, elem C

NOTES

    The default behavior (with a blank selection) is global.  If the
    selection is "all", then the setting entry in each individual
    object will be changed.  Likewise, for a given object, if state is
    zero, then the object setting will be modified.  Otherwise, the
    setting for the indicated state within the object will be
    modified.

    If a selection is provided as opposed to an object name, then the
    atomic setting entries are modified.

    The following per-atom settings are currently implemented.  Others
    may seem to be recognized but will have no effect when set on a
    per-atom basis.
    
    * sphere_color
    * surface_color
    * mesh_color
    * label_color
    * dot_color
    * cartoon_color
    * ribbon_color
    * transparency (for surfaces)
    * sphere_transparency
    
    Note that if you attempt to use the "set" command with a per-bond
    setting over a selection of atoms, the setting change will appear
    to take, but no change will be observed.  Please use the
    "set_bond" command for per-bond settings.
    

PYMOL API

    cmd.set(string name, string value, string selection, int state,
            int updates, int quiet)

SEE ALSO

    get, set_bond
    
'''
        r = DEFAULT_ERROR
        selection = str(selection)
        index = _get_index(name)
        if log:
            name = name_dict.get(index, name)
            _self.log('', "cmd.set('%s',%s,%s,%s)\n" % (name, repr(value), repr(selection), state))
        if True:
            try:
                _self.lock(_self)
                stuple = _cmd.get_setting_tuple(_self._COb,int(index),str(""),int(-1))
                if stuple is None:
                    print("Error: unable to get setting type.")
                    raise QuietException
                type = stuple[0]
                try:
                    if type==1: # boolean (also support non-zero float for truth)
                        handled = 0
                        if boolean_sc.interpret(str(value))==None:
                            try: # number, non-zero, then interpret as TRUE
                                if not (float(value)==0.0):
                                    handled = 1
                                    v = (1,)
                                else:
                                    handled = 1
                                    v = (0,)
                            except:
                                pass
                        if not handled:
                            v = (boolean_dict[
                                boolean_sc.auto_err(
                                str(value),"boolean")],)
                    elif type==2: # int (also supports boolean language for 0,1)
                        if boolean_sc.has_key(str(value)):
                            v = (boolean_dict[
                                boolean_sc.auto_err(
                                str(value),"boolean")],)
                        else:
                            v = (int(value),)
                    elif type==3: # float
                        if boolean_sc.has_key(str(value)):
                            v = (float(boolean_dict[
                                boolean_sc.auto_err(
                                str(value),"boolean")]),)
                        else:
                            v = (float(value),)
                    elif type==4: # float3 - some legacy handling req.
                        if is_string(value):
                            if not ',' in value:
                                v = string.split(value)
                            else:
                                v = eval(value)
                        else:
                            v = value
                        v = (float(v[0]),float(v[1]),float(v[2]))
                    elif type==5: # color
                        v = (str(value),)
                    elif type==6: # string
                        vl = str(value)
                        # strip outermost quotes (cheesy approach)
                        if quote_strip_re.search(vl)!=None:
                            vl=vl[1:-1]
                        v = (vl,)
                    v = (type,v)
                    if len(selection):
                        selection=selector.process(selection)
                    r = _cmd.set(_self._COb,int(index),v,
                                     selection,
                                     int(state)-1,int(quiet),
                                     int(updates))
                except QuietException:
                    pass
                except:
                    if(_feedback(fb_module.cmd,fb_mask.debugging,_self)):
                        traceback.print_exc()
                    raise _self.pymol.CmdException("invalid value: %s" % repr(value))
            finally:
                _self.unlock(r,_self)
        if _self._raising(r,_self): raise QuietException            
        return r

    def unset(name, selection='', state=0, updates=1, log=0, quiet=1, _self=cmd):
        '''
DESCRIPTION

    "unset" clear non-global settings and zeros out global settings.

USAGE

    unset name [,selection [,state ]]

EXAMPLE

    unset orthoscopic

    unset surface_color, 1hpv

    unset sphere_scale, elem C
    
NOTES

    If selection is not provided, unset changes the named global
    setting to a zero or off value.

    If a selection is provided, then "unset" undefines per-object,
    per-state, or per-atom settings.

PYMOL API

    cmd.unset(string name, string selection, int state, int updates,
                int log)

SEE ALSO

    set, set_bond
    
        '''
        r = DEFAULT_ERROR
        selection = str(selection)
        index = _get_index(str(name))
        if log:
            name = name_dict.get(index, name)
            _self.log('', "cmd.unset('%s',%s,%s)\n" % (name, repr(selection), state))
        if True:
                try:
                    _self.lock(_self)
                    try:
                        selection = selector.process(selection)   
                        r = _cmd.unset(_self._COb,int(index),selection,
                                            int(state)-1,int(quiet),
                                            int(updates))
                    except:
                        if(_feedback(fb_module.cmd,fb_mask.debugging,_self)):
                            traceback.print_exc()
                            raise QuietException
                        print("Error: unable to unset setting value.")
                finally:
                    _self.unlock(r,_self)
        return r
    
    def unset_bond(name,selection1,selection2=None,state=0,updates=1,log=0,quiet=1,_self=cmd):
        '''
DESCRIPTION

    "unset_bond" removes a per-bond setting for a given set of bonds.
    
USAGE

    unset name [,selection [, selection [,state ]]]

        '''
        r = DEFAULT_ERROR
        selection1 = str(selection1)
        if selection2 == None:
            selection2 = selection1
        selection2 = str(selection2)
        index = _get_index(str(name))
        if log:
            name = name_dict.get(index, name)
            _self.log('', "cmd.unset_bond('%s',%s,%s,%s)\n" % (name,
                repr(selection1), repr(selection2), state))
        if True:
            try:
                _self.lock(_self)
                try:
                    selection1 = selector.process(selection1)
                    selection2 = selector.process(selection2)   
                    r = _cmd.unset_bond(_self._COb,int(index),selection1,selection2,
                                   int(state)-1,int(quiet),
                                   int(updates))
                except:
                    if(_feedback(fb_module.cmd,fb_mask.debugging,_self)):
                        traceback.print_exc()
                        raise QuietException
                    print("Error: unable to unset setting value.")
            finally:
                _self.unlock(r,_self)
        if _self._raising(r,_self): raise QuietException            
        return r

    def get_setting(name,object='',state=0,_self=cmd): # INTERNAL
        r = DEFAULT_ERROR
        i = _get_index(name)
        try:
            _self.lock(_self)
            r = _cmd.get_setting_tuple(_self._COb,i,str(object),int(state)-1)
            typ = r[0]
            if typ<3: # boolean, int
                value = int(r[1][0])
            elif typ<4: # float
                value = r[1][0]
            elif typ<5: # vector
                value = r[1]
            else:
                value = r[1] # color or string
        finally:
            _self.unlock(r,_self)
        if is_ok(r):
            return value
        elif _self._raising(r,_self):
            raise QuietException                     
        return r

    def get(name, selection='', state=0, quiet=1, _self=cmd):
        '''
DESCRIPTION

    "get" prints out the current value of a setting.

USAGE

    get name [, selection [, state ]]
    
EXAMPLE

    get line_width

ARGUMENTS

    name = string: setting name

    selection = string: object name (selections not yet supported)

    state = integer: state number
    
NOTES

    "get" currently only works with global, per-object, and per-state
    settings.  Atom level settings get be queried with "iterate" (e.g.
    iterate all, print s.line_width)
    
PYMOL API

    cmd.get(string name, string object, int state, int quiet)

SEE ALSO

    set, set_bond, get_bond

    '''
        
        r = DEFAULT_ERROR
        state = int(state)
        i = _get_index(name)
        try:
            _self.lock(_self)
            r = _cmd.get_setting_text(_self._COb,i,str(selection),state-1)
        finally:
            _self.unlock(r,_self)
        if is_ok(r) and (r!=None):
            if not quiet:
                name = name_dict.get(i, name)
                r_str = str(r)
                if len(r_str) > 200:
                    r_str = r_str[:185] + '... (truncated)'
                if(selection==''):
                    print(" get: %s = %s"%(name,r_str))
                elif state<=0:
                    print(" get: %s = %s in object %s"%(name,r_str,selection))
                else:
                    print(" get: %s = %s in object %s state %d"%(name,r_str,selection,state))
        if _self._raising(r,_self): raise QuietException
        return r
    
    def get_setting_tuple(name,object='',state=0,_self=cmd): # INTERNAL
        r = DEFAULT_ERROR
        i = _get_index(name)
        try:
            _self.lock(_self)
            r = _cmd.get_setting_tuple(_self._COb,i,str(object),int(state)-1)
        finally:
            _self.unlock(r,_self)
        if _self._raising(r,_self): raise QuietException
        return r
    
    def get_setting_boolean(name,object='',state=0,_self=cmd): # INTERNAL
        r = DEFAULT_ERROR
        i = _get_index(name)
        try:
            _self.lock(_self)
            r = _cmd.get_setting_of_type(_self._COb,i,str(object),int(state)-1,1)
        finally:
            _self.unlock(r,_self)
        if _self._raising(r,_self): raise QuietException
        return r
    
    def get_setting_int(name,object='',state=0,_self=cmd): # INTERNAL
        r = DEFAULT_ERROR
        i = _get_index(name)
        try:
            _self.lock(_self)
            r = _cmd.get_setting_of_type(_self._COb,i,str(object),int(state)-1,2)
        finally:
            _self.unlock(r,_self)
        return r
    
    def get_setting_float(name,object='',state=0,_self=cmd): # INTERNAL
        r = DEFAULT_ERROR
        i = _get_index(name)
        try:
            _self.lock(_self)
            r = _cmd.get_setting_of_type(_self._COb,i,str(object),int(state)-1,3)
        finally:
            _self.unlock(r,_self)
        if _self._raising(r,_self): raise QuietException
        return r

    def get_setting_text(name,object='',state=0,_self=cmd):  # INTERNAL
        r = DEFAULT_ERROR
        i = _get_index(name)
        try:
            _self.lock(_self)
            r = _cmd.get_setting_text(_self._COb,i,str(object),int(state)-1)
        finally:
            _self.unlock(r,_self)
        if _self._raising(r,_self): raise QuietException
        return r

    def get_setting_updates(object='', state=0, _self=cmd): # INTERNAL
        r = []
        if lock_attempt(_self):
            try:
                r = _cmd.get_setting_updates(_self._COb, object, state-1)
            finally:
                _self.unlock(r,_self)
        return r

    def get_bond(name, selection1, selection2=None,
                 state=0, updates=1, quiet=1, _self=cmd):
        ''' 
DESCRIPTION

    "get_bond" gets per-bond settings for all bonds which exist
    between two selections of atoms.

USAGE

    get_bond name, selection1 [, selection2 ]

ARGUMENTS

    name = string: name of the setting

    selection1 = string: first set of atoms

    selection2 = string: seconds set of atoms {default: (selection1)}

EXAMPLE

    get_bond stick_transparency, */n+c+ca+o


NOTES

    The following per-bond settings are currently implemented.  Others
    may seem to be recognized but will currently have no effect when
    set at the per-bond level.
    
    * valence
    * line_width
    * line_color
    * stick_radius
    * stick_color
    * stick_transparency

PYMOL API

    cmd.get_bond ( string name,
                   string selection1,
                   string selection2,
                   int state, int updates, quiet=1)

       '''
        state, quiet = int(state), int(quiet)
        r = DEFAULT_ERROR
        selection1 = str(selection1)
        if selection2 == None:
            selection2 = selection1

        index = _get_index(str(name))
        if True:
            try:
                _self.lock(_self)
                type = _cmd.get_setting_tuple(_self._COb,int(index),str(""),int(-1))[0]
                if type==None:
                    print("Error: unable to get setting type.")
                    raise QuietException
                try:
                    if len(selection1):
                        selection1=selector.process(selection1)
                    if len(selection2):
                        selection2=selector.process(selection2)                        
                    r = _cmd.get_bond(_self._COb,int(index),
                                 "("+selection1+")","("+selection2+")",
                                 int(state)-1,int(quiet),
                                 int(updates))
                except:
                    traceback.print_exc()
                    if(_feedback(fb_module.cmd,fb_mask.debugging,_self)):
                        traceback.print_exc()
                        print("Error: unable to get_bond info.")
                    raise QuietException
            finally:
                _self.unlock(r,_self)
        if _self._raising(r,_self): raise QuietException            
        if not quiet:
            name = name_dict.get(index, name)
            suffix = ' state %d' % state if state > 0 else ''
            for model, vlist in r:
                print(' %s = %s for object %s' % (name, cmd.get(name, model), model))
                for idx1, idx2, value in vlist:
                    if value is None:
                        continue
                    print(' %s = %s between (%s`%d)-(%s`%d%s)' % (name,
                            value, model, idx1, model, idx2, suffix))
        return r
