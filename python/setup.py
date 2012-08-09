from distutils.core import setup
from distutils.extension import Extension
import sys
import re

def fail(msg):
    sys.stderr.write(msg)
    sys.exit(1)

INC = ['..', 'src/', '../decoder', '../utils', '../mteval']
LIB = ['../decoder', '../utils', '../mteval', '../training', '../klm/lm', '../klm/util']

try:
    with open('../config.status') as config:
        config = config.read()
    subs = dict(re.findall('s,@(\w+)@,\|#_!!_#\|(.*),g', config)) # sed
    if not subs:
        subs = dict(re.findall('S\["(\w+)"\]="(.*)"', config)) # awk
    if not subs:
        fail('Cannot parse config.status\n'
             'Please report this bug to the developers')
    LIBS = re.findall('-l([^\s]+)', subs['LIBS'])
    CPPFLAGS = re.findall('-[^R][^\s]+', subs['CPPFLAGS'])
    LDFLAGS = re.findall('-[^\s]+', subs['LDFLAGS'])
    LDFLAGS = [opt.replace('-R', '-Wl,-rpath,') for opt in LDFLAGS]
except IOError:
    fail('Did you run ./configure? Cannot find config.status')
except KeyError as e:
    fail('Cannot find option {0} in config.status'.format(e))

ext_modules = [
    Extension(name='cdec._cdec',
        sources=['src/_cdec.cpp'],
        include_dirs=INC,
        library_dirs=LIB,
        libraries=LIBS + ['z', 'cdec', 'utils', 'mteval', 'training', 'klm', 'klm_util'],
        extra_compile_args=CPPFLAGS,
        extra_link_args=LDFLAGS),
    Extension(name='cdec.sa._sa',
        sources=['src/sa/_sa.c', 'src/sa/strmap.cc'])
]

setup(
    name='cdec',
    ext_modules=ext_modules,
    requires=['configobj'],
    packages=['cdec', 'cdec.sa'],
    package_dir={'': 'pkg'}
)
