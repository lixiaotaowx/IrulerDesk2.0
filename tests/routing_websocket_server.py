#!/usr/bin/env python3
"""
支持消息路由的WebSocket服务器
用于在PlayerProcess和CaptureProcess之间转发消息
"""

import asyncio
import websockets
import json
import struct
import time
from urllib.parse import urlparse, parse_qs

class RoutingWebSocketServer:
    def __init__(self, host='0.0.0.0', port=8765):
        self.host = host
        self.port = port
        self.publishers = {}  # device_id -> websocket
        self.viewers = {}     # viewer_id -> websocket
        
    async def register_publisher(self, websocket, device_id):
        """注册发布者(CaptureProcess)"""
        self.publishers[device_id] = websocket
        print(f"发布者已连接: {websocket.remote_address}, 设备ID: {device_id}")
        
    async def register_viewer(self, websocket, viewer_id):
        """注册观看者(PlayerProcess)"""
        self.viewers[viewer_id] = websocket
        print(f"观看者已连接: {websocket.remote_address}, 观看者ID: {viewer_id}")
        
    async def unregister_client(self, websocket):
        """注销客户端"""
        # 从发布者中移除
        for device_id, ws in list(self.publishers.items()):
            if ws == websocket:
                del self.publishers[device_id]
                print(f"发布者已断开: {websocket.remote_address}, 设备ID: {device_id}")
                break
                
        # 从观看者中移除
        for viewer_id, ws in list(self.viewers.items()):
            if ws == websocket:
                del self.viewers[viewer_id]
                print(f"观看者已断开: {websocket.remote_address}, 观看者ID: {viewer_id}")
                break
                
    async def route_message(self, sender_ws, message):
        """路由消息"""
        try:
            # 尝试解析JSON消息
            if isinstance(message, str):
                data = json.loads(message)
            else:
                # 二进制消息，尝试解析头部
                if len(message) < 4:
                    return
                header_length = struct.unpack('<I', message[:4])[0]
                if len(message) < 4 + header_length:
                    return
                json_data = message[4:4+header_length].decode('utf-8')
                data = json.loads(json_data)
            
            print(f"收到消息: {data}")
            
            message_type = data.get("type")
            
            if message_type == "watch_request":
                # 观看请求：从viewer转发给publisher
                target_id = data.get("target_id")
                if target_id and target_id in self.publishers:
                    publisher_ws = self.publishers[target_id]
                    print(f"转发观看请求给发布者 {target_id}")
                    await publisher_ws.send(message)
                else:
                    print(f"未找到目标发布者: {target_id}")
                    
            elif message_type == "start_streaming":
                # 开始推流请求：从viewer转发给所有publisher
                print("转发开始推流请求给所有发布者")
                for device_id, publisher_ws in self.publishers.items():
                    try:
                        await publisher_ws.send(message)
                        print(f"已转发给发布者 {device_id}")
                    except Exception as e:
                        print(f"转发给发布者 {device_id} 失败: {e}")
                        
            elif message_type == "stop_streaming":
                # 停止推流请求：从viewer转发给所有publisher
                print("转发停止推流请求给所有发布者")
                for device_id, publisher_ws in self.publishers.items():
                    try:
                        await publisher_ws.send(message)
                        print(f"已转发给发布者 {device_id}")
                    except Exception as e:
                        print(f"转发给发布者 {device_id} 失败: {e}")
                        
            elif message_type in ["tile_metadata", "tile_data", "tile_complete"]:
                # 瓦片数据：从publisher转发给所有viewer
                print(f"转发瓦片数据给所有观看者: {message_type}")
                for viewer_id, viewer_ws in self.viewers.items():
                    try:
                        await viewer_ws.send(message)
                    except Exception as e:
                        print(f"转发给观看者 {viewer_id} 失败: {e}")
                        
        except json.JSONDecodeError as e:
            print(f"JSON解析失败: {e}")
        except Exception as e:
            print(f"消息路由失败: {e}")
            
    async def handle_client(self, websocket):
        """处理客户端连接"""
        path = websocket.path
        print(f"新客户端连接: {websocket.remote_address}, 路径: {path}")
        
        # 解析路径确定客户端类型
        parsed_path = urlparse(path)
        
        if parsed_path.path.startswith('/publish/'):
            # 发布者连接 (CaptureProcess)
            device_id = parsed_path.path.split('/')[-1]
            await self.register_publisher(websocket, device_id)
        else:
            # 观看者连接 (PlayerProcess)
            viewer_id = f"viewer_{int(time.time() * 1000)}"
            await self.register_viewer(websocket, viewer_id)
            
        try:
            # 监听客户端消息并路由
            async for message in websocket:
                await self.route_message(websocket, message)
                
        except websockets.exceptions.ConnectionClosed:
            print("客户端连接已关闭")
        except Exception as e:
            print(f"处理客户端时出错: {e}")
        finally:
            await self.unregister_client(websocket)
            
    async def start_server(self):
        """启动WebSocket服务器"""
        print(f"启动路由WebSocket服务器: ws://{self.host}:{self.port}")
        
        server = await websockets.serve(
            self.handle_client,
            self.host,
            self.port,
            process_request=None
        )
        
        print("路由服务器已启动，等待客户端连接...")
        print("发布者连接路径: /publish/<device_id>")
        print("观看者连接路径: /")
        await server.wait_closed()

async def main():
    server = RoutingWebSocketServer()
    try:
        await server.start_server()
    except KeyboardInterrupt:
        print("\n服务器已停止")

if __name__ == "__main__":
    asyncio.run(main())