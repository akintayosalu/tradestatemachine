# Glossary

Finance and trading terms used in this project, in plain words.

| Term | Meaning |
|---|---|
| **Order** | An instruction to buy or sell a quantity of something. |
| **Bid / Ask** | A buy order / a sell order. The **best bid** is the highest buy price; the **best ask** is the lowest sell price. |
| **Spread** | The gap between the best ask and the best bid. |
| **Limit order** | Buy or sell at a set price *or better*. If it can't trade now, it **rests** in the book. |
| **Market order** | Buy or sell immediately at whatever prices are available. Never rests. |
| **Order book** | All resting orders, grouped by side (bids / asks) and price. |
| **Price level** | All resting orders on one side at one price, queued in arrival order. |
| **Tick** | The smallest price step. Prices here are whole ticks (`10100` = 101.00). |
| **Matching engine** | The program that pairs buy and sell orders into trades. |
| **Price-time priority** | Who trades first: the best price, then, at the same price, whoever arrived first (FIFO). |
| **Crossing** | An incoming order's price reaches the other side (a buy at or above the best ask), so it can trade. |
| **Aggressor / Resting order** | In a trade: the incoming order that crossed / the order already waiting in the book. |
| **Fill / Partial fill** | A trade on an order: completing it / completing only part of it. |
| **Execution report** | A message telling an order's owner what happened to it (`ACCEPT`, `REJECT`, `STATE`, ...). |
| **Cancel / Cancel-replace** | Remove a resting order / change its price or quantity. A replace *loses* its queue place unless it only lowers quantity at the same price. |
| **ClOrdID** | The client's own ID for an order (from FIX). The engine also gives each order an internal ID. |
| **FIX** | The standard messaging protocol between brokers and exchanges. The replace rules here follow FIX style. |
| **Fail-stop** | On an impossible internal error, stop at once rather than continue with possibly corrupt state. |
| **Market data** | The public view of the book (prices and sizes, no owners), published to everyone. |
| **L2 / L3 data** | Market data per price level (total size at each price) / per individual order. |
| **ITCH** | NASDAQ's binary market-data feed format (order added / executed / cancelled). |
| **Failover / Primary-standby** | A backup copy of the engine takes over if the main one dies (Project 2). |
| **Latency / Throughput** | Time per request / requests handled per second. |
| **p50 / p99 / p99.9** | Latency percentiles: half / 99% / 99.9% of requests are faster than this. The higher ones show the **tail**. |
