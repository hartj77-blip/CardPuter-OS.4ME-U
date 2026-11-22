# Sample IR database source

A few remotes written by hand from well-known codes, laid out like
Flipper-IRDB (`<Category>/<Brand>/<Model>.ir`). CI builds the screenshots
and tests from it, and it lets the simulator show the IR Remote app without
downloading anything:

```sh
tools/irdb/build_irdb.py --src tools/irdb/sample --out build/irdb-sample
ZEROOS_IR_DB=$PWD/build/irdb-sample ./build/sim/zeroshell
```

The device ships the full database instead (`build_irdb.py --fetch`, see docs/IR.md).
