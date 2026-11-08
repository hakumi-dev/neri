# Outside-transaction migrations

`Migration` uses `MigrationExecution.Atomic()` in both directions by default.
An atomic migration range runs in one provider transaction with its history
changes. Select `MigrationExecution.OutsideTransaction()` for a direction that
contains one trusted `SchemaOp.Sql` statement requiring autocommit. PostgreSQL
supports this for operations such as `CREATE INDEX CONCURRENTLY`. PostgreSQL and
SQLite both require autocommit for `VACUUM`.

```neri
let onlineIndex = new Migration(
  "add_order_lookup",
  [SchemaOp.Sql("CREATE INDEX CONCURRENTLY order_lookup ON orders (customer_id)")],
  [SchemaOp.Sql("DROP INDEX CONCURRENTLY order_lookup")],
  upExecution: MigrationExecution.OutsideTransaction(),
  downExecution: MigrationExecution.OutsideTransaction()
)
```

Apply or revert an outside-transaction direction as the only selected migration.
Use the existing target argument to bring the database to the boundary first.
Both providers support `migrate` and `migrateAsync` for the isolated transition
on owned connections.
Model scaffolding emits atomic migrations. The guarded SQL generator accepts
atomic ranges. Author outside-transaction operations in a runtime `MigrationSet`
and apply them through a provider. Review trusted SQL and leave the provider's
`__neri_*` history objects intact.

The provider commits an attempt marker before dispatching the SQL. The marker
identifies the migration, direction, exact catalog signature and attempt. On
success, the provider changes the applied history and clears the marker together
in a new transaction. The marker remains when a statement fails or its outcome
cannot be confirmed. Normal migration calls block while the marker is present.
Use a persistent database for recovery across process restarts.

## Resolve an interrupted attempt

Stop other migration runners and ensure the original operation has ended.
Open a provider connection and call `migrationJournal(catalog)` with the same
catalog. `MigrationJournalResult.Loaded` returns the applied prefix and any
pending `MigrationProgress`. Inspect the live database before deciding how to
resolve that attempt. For example, a failed PostgreSQL concurrent index build
can leave an invalid index that requires cleanup.

Pass the returned progress to `recoverMigration(catalog, progress, decision)`.

- `MigrationRecoveryDecision.ConfirmCompleted()` records the verified completed
  direction and clears the marker in one transaction.
- `MigrationRecoveryDecision.ConfirmReverted()` clears the marker after you have
  restored the database to its pre-attempt state. The applied prefix stays the
  same. A later `migrate` call can start a new attempt.

Recovery verifies the attempt and catalog boundary while holding the provider's
migration lock. A stale progress value fails without changing history. The
recovery API updates history only. Database inspection and repair are operator
actions.

PostgreSQL documents the transaction restrictions and failure state of
concurrent index builds. SQLite documents its autocommit and `VACUUM`
requirements. [PostgreSQL CREATE INDEX](https://www.postgresql.org/docs/14/sql-createindex.html#SQL-CREATEINDEX-CONCURRENTLY) · [PostgreSQL VACUUM](https://www.postgresql.org/docs/14/sql-vacuum.html) · [SQLite VACUUM](https://www.sqlite.org/lang_vacuum.html) · [SQLite transactions](https://www.sqlite.org/lang_transaction.html)
