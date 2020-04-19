struct PsInput {
	float4 position : SV_POSITION;
	float2 texCoord : UV;
};

Texture2D textureSimple : register(t0);
SamplerState samplerSimple : register(s0);

float4 PsMain( PsInput _input ) : SV_TARGET {
    return textureSimple.Sample( samplerSimple, _input.texCoord );
}