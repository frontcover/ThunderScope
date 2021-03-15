import { data as sine_data} from '../testdata/sin1MHz';
import { data as sinc_data} from '../testdata/sinc1MHz';

var scope_data: any[] = [];

for(var i = 0; i < 1024; i++) {
  scope_data[i] = {x: i, y: 0};
}

class Range {
  dataMin: number = 0;
  dataMax: number = 0;
  displayLimit: number = 0;

  constructor(limit: number){
    this.displayLimit = limit;
  }

  getDomain() {
    return [this.dataMin, this.dataMin + this.displayLimit];
  }
}

class TestPoints {
  x: Range;
  y: Range;
  data: any[];
  ready: Boolean = true;

  constructor(xRange: number, yRange: number, source: string) {
    this.x = new Range(xRange);
    this.y = new Range(yRange);
    if(source === "sine") {
      this.data = [];
    }
    else if(source === "sinc") {
      this.data = [];
    }
    else {
      this.data = [];
    }
  }

  update() {
    const thunderBridge = (window as any).thunderBridge;

    var testPacket16 = new Uint16Array(new ArrayBuffer(10));
    testPacket16[0] = 0x1F;
    testPacket16[1] = 0x1F2C;
    testPacket16[2] = 4;
    var testPacket = new Uint8Array(testPacket16.buffer);
    testPacket[6] = 1;
    testPacket[7] = 2;
    testPacket[8] = 3;
    testPacket[9] = 4;

    if(!this.ready) {
      return;
    }
    this.ready = false;
    thunderBridge.write(testPacket,() => {
      var rxBuff = new Uint8Array(new ArrayBuffer(6));
      thunderBridge.read(rxBuff, (err: NodeJS.ErrnoException, bytesRead: number, bytes: Uint8Array) => {
        var bytes16 = new Uint16Array(bytes.buffer);
        var dataSize = bytes16[2];
        console.log(bytes16);
        console.log(bytes);
  
        var dataRxBuff = new Uint8Array(dataSize);
        thunderBridge.read(dataRxBuff, (nestedErr: NodeJS.ErrnoException, nestedBytesRead: number, nestedBytes: Uint8Array) => {
          this.ready = true;
          for(var i = 0; i < nestedBytes.length; i++) {
            scope_data[i] = {x: i, y: nestedBytes[i]}
          }
          console.log(nestedBytes);
          console.log(scope_data);
        });
      });
    });
  }

  getData() {
    return scope_data;
  }
}

export default TestPoints;