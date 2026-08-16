FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> fb;
Texture2D<float4> tex : register(t0);
SamplerState samp : register(s0);

float4 main(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy / 64.0;
    fb.WriteSamplerFeedback(tex, samp, uv);
    return float4(1, 0, 0, 1);
}
