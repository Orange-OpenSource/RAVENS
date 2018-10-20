```
"firstReply":
{
	"payload to save":
	{
		"payload signed with device key":
		{
			"signature" : "64B raw signature of reply, signed with the device key",
			"format version" : "2B ID of the manifest format version"
			"updateKey" : "32B Ed25519 public key",
			"hashOfMainManifest" : "32B hash of the main update payload",
			"manifestLength" : "4B integer of the length of the main manifest",
			"old version ID" : "4B integer containing the version on which this update is supposed to install. Restrict the scope of update keys"
			"version ID" : "31b integer containing the version of the firmware. Upgrade only possible with increasing version ID",
			"have hash" : "1b boolean warning of the presence of a hash challenge"			
		},
		"payload signed with update key":
		{
			"challenge reply" : "Update replay protection: SIGN(HASH(Challenge + vID + updateKey + random)). This reply may be used as the challenge of the next update in case of missing proper source of entropy",
			"random" : "8B integer"
		}
	}
	"payload discarded":
	{
		"signature" : "64B signature, signed with the device key",
		"numberOfRanges": "2B"
		"hashRanges": 
		[
			{
				"start"	: "4B address",
				"length": "2B"
			}
		]		
	}
}

"mainManifest" :
{
	"payload":
	{
		"commandsToExecute":
		{
			"__info": "byteField of encoded commands. cf instructions.md"
		},
		"decompressedPayload" :
		{
			"__info" : "payload is compressed, likely using something based on lzfx",
			
			"bindiff":
			{
				"flag" : "0x5ec1714e",
				"initialOffset" : "4B integer indicating the number of pages at the beginning of the flash to ignore when applying the delta. Doesn't affect the execution of commands",
				"nbBinDiff" : "4B",
				"bindiff" :
				[
					{
						"__info" : "this structure is designed to make this chunk streamable",

						"lengthToAddToExisting" : "2B",
						"dataToAddToExisting" : "binary blob",
						"lengthToInsertAfterward" : "2B",
						"dataToInsert" : "binary blob"
					}
				]
			},

			"validation":
			{
				"nbValidations" : "2B",
				"validations" :
				[
					{
						"startToHash" : "4B",
						"length" : "2B",
						"expectedHash" : "32B (SHA-256)"
					}
				]
			}
		}
	}
}
```
