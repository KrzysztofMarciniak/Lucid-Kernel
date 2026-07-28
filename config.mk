# config.mk - project-wide settings, sourced by build.sh
#
# Plain KEY=VALUE lines (no spaces around '='), sourced as shell vars.

# Memory given to the emulated machine in QEMU. Later this also becomes
# the amount our physical memory manager expects to have available.
MEMORY_MB=8
