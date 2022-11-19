# DuckDB external attachment

This extension implements external database attachment for DuckDB, ported as a stopgap from the in progress pull request: https://github.com/duckdb/duckdb/pull/5048

## How to
```bash
git clone https://github.com/kouta-kun/duckdb-external-attach
cd duckdb-external-attach
make
```

Currently building on Ubuntu 20.04 seems to generate a broken extension that does not load on v0.6.0 (produces segfault). Docker instructions seem to work correctly but at a far higher RAM consumption:

```bash
docker build -t external-build .
docker create --name external external-build
docker cp hexhamm:/duckdb-hexhammdist/build/release/extension/duckdb-external-attach/attach_extension.duckdb_extension .
```

## Usage
```bash
$ duckdb -unsigned

INSTALL 'build/release/extension/duckdb-external-attach/attach_extension.duckdb_extension';
LOAD 'attach_extension';
select * from query_external('test.db','test_col');
```
