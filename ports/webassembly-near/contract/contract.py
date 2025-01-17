import near
import json

@near.export
def hello_world():
  input_bytes = near.input(0)
  near.log_utf8("hello_world(): input(0): " + input_bytes.decode("ascii"))
  near.value_return(json.dumps({"input_length": len(input_bytes)}))
