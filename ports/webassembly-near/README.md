MicroPython WebAssembly NEAR
============================

MicroPython for [WebAssembly](https://webassembly.org/) and specifically [NEAR Protocol](https://near.org) runtime

Dependencies
------------

Building the webassembly port bears the same requirements as the standard
MicroPython ports with the addition of Emscripten.

The output is `contract.wasm` which is ready for deployment on the NEAR Protocol blockchain.

Build instructions
------------------

In order to build `contract.wasm`, run:

    $ make

Contract source code
--------------------

Python contract source code is assumed to be located in `contract/contract.py`. Any other .py files placed into `contact/` will be packaged into the resulting WASM file and accessible from `contract.py` via `import ...`

Contract functions to be exported from the WASM file should be:
- decorated with `@near.export`
- provided with a proxy `void <contract function name>()` function in `main.c` calling `run_frozen_fn("contract.py", "<contract function name>");`
- added to `EXPORTED_FUNCTIONS` list in the `Makefile`

Last two requirements will be removed in the future.

API
---

At the moment, the following NEAR Protocol API functions are implemented:

- `near.input(register_id)`: returns a bytes object with the data from the requested register
- `near.value_return(bytes)`: provides a return value to the NEAR VM
- `log_utf8(str)`: logs an utf-8 string to the NEAR VM

These are implemented in `modnear.c`, more API functions can be implemented in a similar fashion where needed

MicroPython compatibility
-------------------------

Catching Python exceptions is not currently supported due to WASM exceptions being unavailable in the NEAR Protocol runtime. Raising an exception will terminate contract execution.

Most other things which make sense within runtime environment should work as nothing was specifically removed from MicroPython to make this compatible with the NEAR Protocol runtime.
