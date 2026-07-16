# Harbor startup package

This manually authored multi-module package is the `startup/1` representative
application. It exercises imports, typed inputs, an enum guard, a dynamic weight,
parameters, ordered bindings, ordinary composition, and a complete external
message. Benchmarks use the exported ordinary `harbor.scene` entry when no
formatter is present.

The package is deliberately modest: it represents an application-shaped startup
case rather than an adversarial graph generator. Any bytecode decision must report
it separately from `workloads/1` stress cases.
