struct PsInput {
	float4 position : SV_POSITION;
	float2 texCoord : UV;
};

cbuffer transform : register(b0) {
    float4 offset;
};

PsInput VsMain(float3 _pos : POSITION, float2 _texCoord : TEXCOORD ) {
	PsInput output;
	output.texCoord = _texCoord;
	output.position = float4( _pos, 1.0f ) + offset;
	return output;
}