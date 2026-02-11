// Stencil update pass for chunked transmittance culling

[[vk::input_attachment_index(0)]]
[[vk::binding(0, 0)]] SubpassInput<float4> accumInput;

struct StencilUpdatePC
{
    float transmittanceThreshold;
};
[[vk::push_constant]] StencilUpdatePC pc;

void main()
{
    // Reads transmittance from color attachment and marks saturated pixels in stencil
    float4 accum = accumInput.SubpassLoad();

    float transmittance = accum.a;

    if (transmittance >= pc.transmittanceThreshold)
    {
        // Pixel is not saturated yet, discard to avoid stencil write
        // This keeps the stencil at 0 so subsequent splats will still be rendered
        discard;
    }
}
