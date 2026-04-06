from ipykernel.kernelapp import IPKernelApp
from .kernel import UCoreKernel

IPKernelApp.launch_instance(kernel_class=UCoreKernel)
