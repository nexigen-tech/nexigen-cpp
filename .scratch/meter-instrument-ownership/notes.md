# Meter instrument ownership

Исходная идея: `Meter` возвращает не сырой указатель на метрику, а умный указатель
вида `std::shared_ptr<Counter<>>` (см. историю explore-сессии).

## Resolution (2026-11-22)

Отклонено. Инструменты — заимствованные хэндлы; единственный владелец —
`MetricProvider`. `shared_ptr` с no-op делитером даёт ложное владение и аллокацию
контрольного блока, а реальное совладение ломает детерминированный shutdown.
Принятый инвариант: хэндл memory-safe, пока жив объект `Infrastructure`;
`shutdown()` выполняет quiesce, освобождение памяти — в `~Infrastructure`.

- Решение и последствия: `docs/adr/0002-telemetry-handle-lifetime.md`
- Реализация и контракт: change `keep-telemetry-handles-valid-until-infrastructure-destruction`
  (archived 2026-11-21).
