#!/usr/bin/env python
import os
import sys

if __name__ == '__main__':
    import configure

    configure_options = [
    '--with-mpi-dir=/Users/petsc/software/mpich-1.2.5.2',
    '--with-mpi-compilers=0'
    ]

    configure.petsc_configure(configure_options)
