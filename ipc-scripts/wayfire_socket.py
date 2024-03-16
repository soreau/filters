import socket
import json as js

def get_msg_template(method: str):
    # Create generic message template
    message = {}
    message["method"] = method
    message["data"] = {}
    return message

class WayfireSocket:
    def __init__(self, socket_name):
        self.client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.client.connect(socket_name)

    def read_exact(self, n):
        response = bytes()
        while n > 0:
            read_this_time = self.client.recv(n)
            if not read_this_time:
                raise Exception("Failed to read anything from the socket!")
            n -= len(read_this_time)
            response += read_this_time

        return response

    def read_message(self):
        rlen = int.from_bytes(self.read_exact(4), byteorder="little")
        response_message = self.read_exact(rlen)
        return js.loads(response_message)

    def send_json(self, msg):
        data = js.dumps(msg).encode('utf8')
        header = len(data).to_bytes(4, byteorder="little")
        self.client.send(header)
        self.client.send(data)
        return self.read_message()

    def set_view_shader(self, view_id: int, shader: str):
        message = get_msg_template("wf/filters/set-view-shader")
        message["data"] = {}
        message["data"]["view-id"] = view_id
        message["data"]["shader-path"] = shader
        return self.send_json(message)

    def unset_view_shader(self, view_id: int):
        message = get_msg_template("wf/filters/unset-view-shader")
        message["data"] = {}
        message["data"]["view-id"] = view_id
        return self.send_json(message)

    def view_has_shader(self, view_id: int):
        message = get_msg_template("wf/filters/view-has-shader")
        message["data"] = {}
        message["data"]["view-id"] = view_id
        return self.send_json(message)

    def set_fs_shader(self, output_name: int, shader: str):
        message = get_msg_template("wf/filters/set-fs-shader")
        message["data"] = {}
        message["data"]["output-name"] = output_name
        message["data"]["shader-path"] = shader
        return self.send_json(message)

    def unset_fs_shaders(self):
        message = get_msg_template("wf/filters/unset-fs-shader")
        message["data"] = {}
        return self.send_json(message)

    def fs_has_shader(self):
        message = get_msg_template("wf/filters/fs-has-shader")
        message["data"] = {}
        return self.send_json(message)
