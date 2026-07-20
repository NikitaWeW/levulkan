struct CameraMatrixData {
    column_major float4x4 projMat;
    column_major float4x4 viewMat;
};
struct ModelMatrixData {
    column_major float4x4 modelMat;
    column_major float4x4 normMat;
};