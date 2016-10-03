// HLSL shader version 4.0 (for Direct3D 11/ 12)

struct InputVS
{
	float2	position		: POSITION;
	float3	color			: COLOR;
	float3	instanceColor	: INSTANCECOLOR;
	float2	instanceOffset	: INSTANCEOFFSET;
	float	instanceScale	: INSTANCESCALE;
};

struct OutputVS
{
	float4 position	: SV_Position;
	float3 color	: COLOR;
};

// Vertex shader main function
OutputVS VS(InputVS inp)
{
	OutputVS outp;
	outp.position = float4(inp.position * inp.instanceScale + inp.instanceOffset, 0, 1);
	outp.color = inp.instanceColor * inp.color;
	return outp;
}

// Pixel shader main function
float4 PS(OutputVS inp) : SV_Target
{
	return float4(inp.color, 1);
};

