struct CameraData {
    column_major float4x4 projMat;
    column_major float4x4 viewMat;
};
struct ModelData {
    column_major float4x4 modelMat;
    column_major float4x4 normMat;
};