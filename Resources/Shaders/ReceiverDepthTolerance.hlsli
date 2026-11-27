// 探索と深度の確認で同じ許容範囲を使う。D32 の
// 再構成の誤差は距離とともに大きくなり、特にかすめる受け手で大きい。
float ReceiverDepthTolerance(float viewDepth){return max(.2,viewDepth*.002);}
float ReceiverTraceMaximum(float distance,float viewDepth){return distance+ReceiverDepthTolerance(viewDepth);}
