import os
from building import *

cwd = GetCurrentDir()
src = Glob('src/*.c') + Glob('ports/*.c')
path = [os.path.join(cwd, 'include'), os.path.join(cwd, 'ports')]

group = DefineGroup('MicroscopeScope', src,
                    depend=['RT_USING_SCOPE'], CPPPATH=path)

for item in ('arch', 'backtrace', 'coredump', 'ports'):
    group += SConscript(os.path.join(item, 'SConscript'))

Return('group')
