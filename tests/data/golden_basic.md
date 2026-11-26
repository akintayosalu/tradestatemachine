ADD seq=1 orderId=1 side=Sell px=10100 qty=30
ADD seq=2 orderId=2 side=Sell px=10100 qty=20
ADD seq=3 orderId=3 side=Sell px=10200 qty=50
ADD seq=4 orderId=4 side=Buy px=9900 qty=40
ADD seq=5 orderId=5 side=Buy px=9800 qty=10
EXEC seq=6 orderId=1 qty=30 px=10100 tradeId=1
EXEC seq=7 orderId=2 qty=20 px=10100 tradeId=2
EXEC seq=8 orderId=3 qty=10 px=10200 tradeId=3
DELETE seq=9 orderId=5
ADD seq=10 orderId=8 side=Buy px=9900 qty=25
MODIFY seq=11 orderId=4 newOrderId=9 qty=30
DELETE seq=12 orderId=8
EXEC seq=13 orderId=3 qty=25 px=10200 tradeId=4
EXEC seq=14 orderId=9 qty=30 px=9900 tradeId=5
EXEC seq=15 orderId=3 qty=15 px=10200 tradeId=6
