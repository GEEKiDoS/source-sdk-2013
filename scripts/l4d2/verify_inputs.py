"""Shared, side-effect-free checks for in-place L4D2 deployment."""

LAYERS = ('update', 'left4dead2_dlc3', 'left4dead2_dlc2', 'left4dead2_dlc1', 'left4dead2')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)
