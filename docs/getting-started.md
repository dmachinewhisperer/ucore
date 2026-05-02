# Getting started

After [flashing your board](flash.md), install the kernel and open a
notebook.

## 1 · Install the kernel

```bash
pip install ucore
```

The wheel ships with the µcore Jupyter kernelspec — `jupyter lab`
will pick it up automatically.

??? note "Custom Python setups (pyenv, conda)"
    If `which python3` doesn't resolve to the interpreter you want the
    kernel to run on, bind the kernelspec explicitly:

    ```bash
    python -m ukernel install            # use this interpreter
    python -m ukernel install --user     # to ~/.local/share/jupyter
    ```

## 2 · Launch JupyterLab

```bash
jupyter lab
```

The launcher shows a **µcore** tile next to **Python 3**. Open it.

## 3 · Your first cell

```python
%%ucore
print("hello from the device")
```

The `%%ucore` magic sends the cell to the connected board. The next
section, [Pipes](#), covers streaming data back from the device into
host-side cells (live plots, logging, etc.) — coming soon.
