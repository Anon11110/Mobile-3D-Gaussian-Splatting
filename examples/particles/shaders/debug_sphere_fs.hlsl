// Simple fragment shader that outputs a solid bright white color.
// This is used for the wireframe debug spheres to make them easily visible.

struct PSOutput
{
	float4 color : SV_Target0;
};

PSOutput main()
{
	PSOutput output;
	// Output a constant, bright white color for the wireframe lines.
	output.color = float4(1.0, 1.0, 1.0, 1.0);
	return output;
}
