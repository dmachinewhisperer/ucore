import sys

# `python -m ukernel install [...]` registers the kernelspec bound to
# this interpreter. Anything else falls through to the kernel launcher
# (used by jupyter_client when the spec's argv fires us up).
if len(sys.argv) > 1 and sys.argv[1] == "install":
    from .install import main
    main(sys.argv[2:])
else:
    from ipykernel.kernelapp import IPKernelApp
    from .kernel import UCoreKernel
    IPKernelApp.launch_instance(kernel_class=UCoreKernel)
