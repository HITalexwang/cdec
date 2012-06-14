from distutils.core import setup
from distutils.extension import Extension
from Cython.Distutils import build_ext
import os

INC = ['..', 'src/', '../decoder', '../utils']
LIB = ['../decoder', '../utils', '../mteval', '../klm/lm', '../klm/util']

BOOST_ROOT = os.getenv('BOOST_ROOT')
if BOOST_ROOT:
    INC.append(os.path.join(BOOST_ROOT, 'include/'))
    LIB.append(os.path.join(BOOST_ROOT, 'lib/'))

ext_modules = [
    Extension(name='_cdec',
        sources=['src/_cdec.pyx'],
        language='C++', 
        include_dirs=INC,
        library_dirs=LIB,
        libraries=['boost_program_options-mt', 'z',
                   'cdec', 'utils', 'mteval', 'klm', 'klm_util'])
]

setup(
    name='cdec',
    cmdclass={'build_ext': build_ext},
    ext_modules=ext_modules,
    packages=['cdec', 'cdec.scfg']
)
