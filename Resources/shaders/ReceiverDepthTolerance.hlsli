// Use the same acceptance interval for traversal and depth validation. D32
// reconstruction error grows with distance, especially on grazing receivers.
float ReceiverDepthTolerance(float viewDepth){return max(.2,viewDepth*.002);}
float ReceiverTraceMaximum(float distance,float viewDepth){return distance+ReceiverDepthTolerance(viewDepth);}
