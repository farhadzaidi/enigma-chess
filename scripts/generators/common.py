"""Shared utilities for code generators."""


def write_array(file, name, array, type, align=False):
    """Write a C++ array declaration with flat-initializer syntax."""
    dim = ''.join(f'[{d}]' for d in array.shape)
    flat = ', '.join(str(x) for x in array.flatten())
    prefix = 'alignas(64) ' if align else ''
    file.write(f'{prefix}inline const {type} {name}{dim} = {{ {flat} }};\n')