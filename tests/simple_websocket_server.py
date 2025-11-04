#!/usr/bin/env python3
"""
简单的WebSocket服务器，用于测试瓦片渲染集成
模拟发送瓦片元数据和瓦片数据
"""

import asyncio
import websockets
import json
import struct
import time
import random

class TileWebSocketServer:
    def __init__(self, host='localhost', port=8080):
        self.host = host
        self.port = port
        self.clients = set()
        
    async def register_client(self, websocket):
        """注册新客户端"""
        self.clients.add(websocket)
        print(f"客户端已连接: {websocket.remote_address}")
        
    async def unregister_client(self, websocket):
        """注销客户端"""
        self.clients.discard(websocket)
        print(f"客户端已断开: {websocket.remote_address}")
        
    async def send_tile_metadata(self, websocket, tile_id):
        """发送瓦片元数据"""
        metadata = {
            "type": "tile_metadata",
            "tile_id": tile_id,  # 使用正确的字段名
            "x": 0,
            "y": 0,
            "width": 256,
            "height": 256,
            "total_chunks": 4,  # 使用正确的字段名
            "data_size": 256 * 256 * 4,  # 完整瓦片大小
            "timestamp": int(time.time() * 1000),
            "format": "RGBA"
        }
        
        # 构造二进制消息格式：4字节头部长度 + JSON头部
        json_data = json.dumps(metadata).encode('utf-8')
        header_length = len(json_data)
        
        # 构造完整消息
        message = bytearray()
        message.extend(struct.pack('<I', header_length))  # 4字节头部长度
        message.extend(json_data)  # JSON头部
        # 元数据消息没有二进制数据部分
        
        await websocket.send(message)
        print(f"发送瓦片元数据: {metadata}")
        
    async def send_tile_chunk(self, websocket, tile_id, chunk_id):
        """发送瓦片数据块"""
        # 创建模拟的瓦片数据 (256x256 RGBA)
        chunk_size = 256 * 64 * 4  # 每个块64行
        tile_data = bytearray(chunk_size)
        
        # 填充一些颜色数据 (简单的渐变)
        for i in range(0, chunk_size, 4):
            tile_data[i] = (i // 4) % 256      # R
            tile_data[i+1] = ((i // 4) * 2) % 256  # G  
            tile_data[i+2] = ((i // 4) * 3) % 256  # B
            tile_data[i+3] = 255               # A
            
        # 构造瓦片块消息头
        header = {
            "type": "tile_data",
            "tile_id": tile_id,
            "chunk_index": chunk_id,
            "total_chunks": 4,
            "timestamp": int(time.time() * 1000)
        }
        
        # 构造二进制消息格式：4字节头部长度 + JSON头部 + 二进制数据
        json_data = json.dumps(header).encode('utf-8')
        header_length = len(json_data)
        
        # 构造完整消息
        message = bytearray()
        message.extend(struct.pack('<I', header_length))  # 4字节头部长度
        message.extend(json_data)  # JSON头部
        message.extend(tile_data)  # 二进制数据
        
        await websocket.send(message)
        print(f"发送瓦片块: tileId={tile_id}, chunkId={chunk_id}, size={len(tile_data)}")
        
    async def send_tile_update(self, websocket, tile_id):
        """发送瓦片更新完成消息"""
        update_msg = {
            "type": "tile_complete",
            "tile_id": tile_id,
            "timestamp": int(time.time() * 1000)
        }
        
        # 构造二进制消息格式：4字节头部长度 + JSON头部
        json_data = json.dumps(update_msg).encode('utf-8')
        header_length = len(json_data)
        
        # 构造完整消息
        message = bytearray()
        message.extend(struct.pack('<I', header_length))  # 4字节头部长度
        message.extend(json_data)  # JSON头部
        # 更新消息没有二进制数据部分
        
        await websocket.send(message)
        print(f"发送瓦片更新: {update_msg}")
        
    async def simulate_tile_transmission(self, websocket):
        """模拟瓦片传输过程"""
        try:
            # 发送3个瓦片
            for tile_id in range(1, 4):
                print(f"\n开始传输瓦片 {tile_id}")
                
                # 1. 发送瓦片元数据
                await self.send_tile_metadata(websocket, tile_id)
                await asyncio.sleep(0.1)
                
                # 2. 发送瓦片数据块
                for chunk_id in range(4):
                    await self.send_tile_chunk(websocket, tile_id, chunk_id)
                    await asyncio.sleep(0.1)
                
                # 3. 发送瓦片完成消息
                await self.send_tile_update(websocket, tile_id)
                await asyncio.sleep(0.2)
                
            print("\n所有瓦片传输完成")
            
        except Exception as e:
            print(f"瓦片传输过程中出错: {e}")
            
    async def handle_client(self, websocket):
        """处理客户端连接"""
        await self.register_client(websocket)
        
        try:
            # 启动瓦片传输任务
            transmission_task = asyncio.create_task(
                self.simulate_tile_transmission(websocket)
            )
            
            # 监听客户端消息
            async for message in websocket:
                try:
                    data = json.loads(message)
                    print(f"收到客户端消息: {data}")
                    
                    if data.get("type") == "watch_request":
                        print("客户端请求观看，开始发送瓦片数据")
                        
                except json.JSONDecodeError:
                    print(f"收到非JSON消息: {message}")
                    
        except websockets.exceptions.ConnectionClosed:
            print("客户端连接已关闭")
        finally:
            await self.unregister_client(websocket)
            
    async def start_server(self):
        """启动WebSocket服务器"""
        print(f"启动WebSocket服务器: ws://{self.host}:{self.port}")
        
        server = await websockets.serve(
            self.handle_client,
            self.host,
            self.port
        )
        
        print("服务器已启动，等待客户端连接...")
        await server.wait_closed()

async def main():
    server = TileWebSocketServer()
    try:
        await server.start_server()
    except KeyboardInterrupt:
        print("\n服务器已停止")

if __name__ == "__main__":
    asyncio.run(main())