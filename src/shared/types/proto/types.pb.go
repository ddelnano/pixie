// Code generated by protoc-gen-gogo. DO NOT EDIT.
// source: src/shared/types/proto/types.proto

package typespb

import (
	fmt "fmt"
	proto "github.com/gogo/protobuf/proto"
	io "io"
	math "math"
	math_bits "math/bits"
	reflect "reflect"
	strconv "strconv"
	strings "strings"
)

// Reference imports to suppress errors if they are not otherwise used.
var _ = proto.Marshal
var _ = fmt.Errorf
var _ = math.Inf

// This is a compile-time assertion to ensure that this generated file
// is compatible with the proto package it is being compiled against.
// A compilation error at this line likely means your copy of the
// proto package needs to be updated.
const _ = proto.GoGoProtoPackageIsVersion3 // please upgrade the proto package

type DataType int32

const (
	DATA_TYPE_UNKNOWN DataType = 0
	BOOLEAN           DataType = 1
	INT64             DataType = 2
	UINT128           DataType = 3
	FLOAT64           DataType = 4
	STRING            DataType = 5
	TIME64NS          DataType = 6
)

var DataType_name = map[int32]string{
	0: "DATA_TYPE_UNKNOWN",
	1: "BOOLEAN",
	2: "INT64",
	3: "UINT128",
	4: "FLOAT64",
	5: "STRING",
	6: "TIME64NS",
}

var DataType_value = map[string]int32{
	"DATA_TYPE_UNKNOWN": 0,
	"BOOLEAN":           1,
	"INT64":             2,
	"UINT128":           3,
	"FLOAT64":           4,
	"STRING":            5,
	"TIME64NS":          6,
}

func (DataType) EnumDescriptor() ([]byte, []int) {
	return fileDescriptor_6a0f3bae72116e90, []int{0}
}

type PatternType int32

const (
	UNSPECIFIED    PatternType = 0
	GENERAL        PatternType = 100
	GENERAL_ENUM   PatternType = 101
	STRUCTURED     PatternType = 200
	METRIC_COUNTER PatternType = 300
	METRIC_GAUGE   PatternType = 301
)

var PatternType_name = map[int32]string{
	0:   "UNSPECIFIED",
	100: "GENERAL",
	101: "GENERAL_ENUM",
	200: "STRUCTURED",
	300: "METRIC_COUNTER",
	301: "METRIC_GAUGE",
}

var PatternType_value = map[string]int32{
	"UNSPECIFIED":    0,
	"GENERAL":        100,
	"GENERAL_ENUM":   101,
	"STRUCTURED":     200,
	"METRIC_COUNTER": 300,
	"METRIC_GAUGE":   301,
}

func (PatternType) EnumDescriptor() ([]byte, []int) {
	return fileDescriptor_6a0f3bae72116e90, []int{1}
}

type SemanticType int32

const (
	ST_UNSPECIFIED             SemanticType = 0
	ST_NONE                    SemanticType = 1
	ST_AGENT_UID               SemanticType = 100
	ST_ASID                    SemanticType = 101
	ST_UPID                    SemanticType = 200
	ST_SERVICE_NAME            SemanticType = 300
	ST_POD_NAME                SemanticType = 400
	ST_POD_PHASE               SemanticType = 401
	ST_POD_STATUS              SemanticType = 402
	ST_NODE_NAME               SemanticType = 500
	ST_CONTAINER_NAME          SemanticType = 600
	ST_CONTAINER_STATE         SemanticType = 601
	ST_CONTAINER_STATUS        SemanticType = 602
	ST_NAMESPACE_NAME          SemanticType = 700
	ST_BYTES                   SemanticType = 800
	ST_PERCENT                 SemanticType = 900
	ST_DURATION_NS             SemanticType = 901
	ST_THROUGHPUT_PER_NS       SemanticType = 902
	ST_THROUGHPUT_BYTES_PER_NS SemanticType = 903
	ST_QUANTILES               SemanticType = 1000
	ST_DURATION_NS_QUANTILES   SemanticType = 1001
	ST_IP_ADDRESS              SemanticType = 1100
	ST_PORT                    SemanticType = 1200
	ST_HTTP_REQ_METHOD         SemanticType = 1300
	ST_HTTP_RESP_STATUS        SemanticType = 1400
	ST_HTTP_RESP_MESSAGE       SemanticType = 1500
)

var SemanticType_name = map[int32]string{
	0:    "ST_UNSPECIFIED",
	1:    "ST_NONE",
	100:  "ST_AGENT_UID",
	101:  "ST_ASID",
	200:  "ST_UPID",
	300:  "ST_SERVICE_NAME",
	400:  "ST_POD_NAME",
	401:  "ST_POD_PHASE",
	402:  "ST_POD_STATUS",
	500:  "ST_NODE_NAME",
	600:  "ST_CONTAINER_NAME",
	601:  "ST_CONTAINER_STATE",
	602:  "ST_CONTAINER_STATUS",
	700:  "ST_NAMESPACE_NAME",
	800:  "ST_BYTES",
	900:  "ST_PERCENT",
	901:  "ST_DURATION_NS",
	902:  "ST_THROUGHPUT_PER_NS",
	903:  "ST_THROUGHPUT_BYTES_PER_NS",
	1000: "ST_QUANTILES",
	1001: "ST_DURATION_NS_QUANTILES",
	1100: "ST_IP_ADDRESS",
	1200: "ST_PORT",
	1300: "ST_HTTP_REQ_METHOD",
	1400: "ST_HTTP_RESP_STATUS",
	1500: "ST_HTTP_RESP_MESSAGE",
}

var SemanticType_value = map[string]int32{
	"ST_UNSPECIFIED":             0,
	"ST_NONE":                    1,
	"ST_AGENT_UID":               100,
	"ST_ASID":                    101,
	"ST_UPID":                    200,
	"ST_SERVICE_NAME":            300,
	"ST_POD_NAME":                400,
	"ST_POD_PHASE":               401,
	"ST_POD_STATUS":              402,
	"ST_NODE_NAME":               500,
	"ST_CONTAINER_NAME":          600,
	"ST_CONTAINER_STATE":         601,
	"ST_CONTAINER_STATUS":        602,
	"ST_NAMESPACE_NAME":          700,
	"ST_BYTES":                   800,
	"ST_PERCENT":                 900,
	"ST_DURATION_NS":             901,
	"ST_THROUGHPUT_PER_NS":       902,
	"ST_THROUGHPUT_BYTES_PER_NS": 903,
	"ST_QUANTILES":               1000,
	"ST_DURATION_NS_QUANTILES":   1001,
	"ST_IP_ADDRESS":              1100,
	"ST_PORT":                    1200,
	"ST_HTTP_REQ_METHOD":         1300,
	"ST_HTTP_RESP_STATUS":        1400,
	"ST_HTTP_RESP_MESSAGE":       1500,
}

func (SemanticType) EnumDescriptor() ([]byte, []int) {
	return fileDescriptor_6a0f3bae72116e90, []int{2}
}

type UInt128 struct {
	Low  uint64 `protobuf:"varint,1,opt,name=low,proto3" json:"low,omitempty"`
	High uint64 `protobuf:"varint,2,opt,name=high,proto3" json:"high,omitempty"`
}

func (m *UInt128) Reset()      { *m = UInt128{} }
func (*UInt128) ProtoMessage() {}
func (*UInt128) Descriptor() ([]byte, []int) {
	return fileDescriptor_6a0f3bae72116e90, []int{0}
}
func (m *UInt128) XXX_Unmarshal(b []byte) error {
	return m.Unmarshal(b)
}
func (m *UInt128) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	if deterministic {
		return xxx_messageInfo_UInt128.Marshal(b, m, deterministic)
	} else {
		b = b[:cap(b)]
		n, err := m.MarshalToSizedBuffer(b)
		if err != nil {
			return nil, err
		}
		return b[:n], nil
	}
}
func (m *UInt128) XXX_Merge(src proto.Message) {
	xxx_messageInfo_UInt128.Merge(m, src)
}
func (m *UInt128) XXX_Size() int {
	return m.Size()
}
func (m *UInt128) XXX_DiscardUnknown() {
	xxx_messageInfo_UInt128.DiscardUnknown(m)
}

var xxx_messageInfo_UInt128 proto.InternalMessageInfo

func (m *UInt128) GetLow() uint64 {
	if m != nil {
		return m.Low
	}
	return 0
}

func (m *UInt128) GetHigh() uint64 {
	if m != nil {
		return m.High
	}
	return 0
}

func init() {
	proto.RegisterEnum("pl.types.DataType", DataType_name, DataType_value)
	proto.RegisterEnum("pl.types.PatternType", PatternType_name, PatternType_value)
	proto.RegisterEnum("pl.types.SemanticType", SemanticType_name, SemanticType_value)
	proto.RegisterType((*UInt128)(nil), "pl.types.UInt128")
}

func init() {
	proto.RegisterFile("src/shared/types/proto/types.proto", fileDescriptor_6a0f3bae72116e90)
}

var fileDescriptor_6a0f3bae72116e90 = []byte{
	// 672 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x64, 0x53, 0x3d, 0x4f, 0x1b, 0x4b,
	0x14, 0xf5, 0xda, 0xc6, 0x6b, 0xae, 0x0d, 0x0c, 0x03, 0xef, 0x3d, 0xbf, 0x48, 0xd9, 0x44, 0x54,
	0x11, 0x05, 0x16, 0x04, 0x21, 0x9a, 0x14, 0x83, 0x77, 0xb0, 0x47, 0xb1, 0x67, 0x97, 0x99, 0x3b,
	0x89, 0x48, 0x33, 0x32, 0x60, 0x05, 0x24, 0x3e, 0x2c, 0x63, 0x25, 0xa2, 0x4b, 0x91, 0x8f, 0x96,
	0x44, 0xf9, 0x01, 0x29, 0x53, 0x24, 0x52, 0x8a, 0x94, 0xf9, 0x01, 0x14, 0x29, 0x28, 0x49, 0x94,
	0x22, 0x98, 0x86, 0x74, 0x14, 0x29, 0x52, 0x46, 0xbb, 0x5e, 0x2b, 0x41, 0x74, 0xf7, 0x9e, 0x73,
	0xf7, 0x9c, 0x3b, 0x67, 0x75, 0x61, 0x6a, 0xbf, 0xb3, 0x5e, 0xde, 0xdf, 0x6c, 0x76, 0x5a, 0x1b,
	0xe5, 0xee, 0x41, 0xbb, 0xb5, 0x5f, 0x6e, 0x77, 0xf6, 0xba, 0x7b, 0xfd, 0x7a, 0x26, 0xae, 0x69,
	0xbe, 0xbd, 0x3d, 0x13, 0xf7, 0x53, 0x65, 0x70, 0x8d, 0xd8, 0xed, 0xce, 0xce, 0x2d, 0x52, 0x02,
	0x99, 0xed, 0xbd, 0xc7, 0x25, 0xe7, 0xa6, 0x73, 0x2b, 0xab, 0xa2, 0x92, 0x52, 0xc8, 0x6e, 0x6e,
	0x3d, 0xdc, 0x2c, 0xa5, 0x63, 0x28, 0xae, 0xa7, 0x77, 0x20, 0xef, 0x37, 0xbb, 0x4d, 0x3c, 0x68,
	0xb7, 0xe8, 0x3f, 0x30, 0xee, 0x33, 0x64, 0x16, 0x57, 0x43, 0x6e, 0x8d, 0xbc, 0x2b, 0x83, 0xfb,
	0x92, 0xa4, 0x68, 0x01, 0xdc, 0xa5, 0x20, 0xa8, 0x73, 0x26, 0x89, 0x43, 0x87, 0x61, 0x48, 0x48,
	0x5c, 0x98, 0x27, 0xe9, 0x08, 0x37, 0x42, 0xe2, 0xec, 0xdc, 0x22, 0xc9, 0x44, 0xcd, 0x72, 0x3d,
	0x60, 0x11, 0x93, 0xa5, 0x00, 0x39, 0x8d, 0x4a, 0xc8, 0x2a, 0x19, 0xa2, 0x45, 0xc8, 0xa3, 0x68,
	0xf0, 0x85, 0x79, 0xa9, 0x49, 0x6e, 0xfa, 0x11, 0x14, 0xc2, 0x66, 0xb7, 0xdb, 0xea, 0xec, 0xc6,
	0x8e, 0x63, 0x50, 0x30, 0x52, 0x87, 0xbc, 0x22, 0x96, 0x05, 0xf7, 0xfb, 0x5e, 0x55, 0x2e, 0xb9,
	0x62, 0x75, 0xb2, 0x41, 0x09, 0x14, 0x93, 0xc6, 0x72, 0x69, 0x1a, 0x24, 0x9a, 0x07, 0x8d, 0xca,
	0x54, 0xd0, 0x28, 0xee, 0x93, 0x23, 0x87, 0x4e, 0xc0, 0x68, 0x83, 0xa3, 0x12, 0x15, 0x5b, 0x09,
	0x8c, 0x44, 0xae, 0xc8, 0xbb, 0x34, 0x1d, 0x87, 0x62, 0x02, 0x56, 0x99, 0xa9, 0x72, 0xf2, 0x3e,
	0x3d, 0xfd, 0x31, 0x0b, 0x45, 0xdd, 0xda, 0x69, 0xee, 0x76, 0xb7, 0xd6, 0x63, 0x67, 0x0a, 0xa3,
	0x1a, 0xed, 0x15, 0x73, 0x8d, 0x56, 0x06, 0x92, 0x13, 0x27, 0x32, 0xd7, 0x68, 0x59, 0x95, 0x4b,
	0xb4, 0x46, 0xf8, 0x64, 0x23, 0xa1, 0x99, 0x16, 0x3e, 0x69, 0xd1, 0x62, 0xdc, 0x98, 0x50, 0xc4,
	0x6b, 0x4c, 0xc2, 0x98, 0x46, 0xab, 0xb9, 0xba, 0x27, 0x2a, 0xdc, 0x4a, 0xd6, 0xe0, 0xd1, 0x1e,
	0x04, 0x0a, 0x1a, 0x6d, 0x18, 0xf8, 0x7d, 0xe4, 0x30, 0x13, 0x6d, 0x96, 0x20, 0x61, 0x8d, 0x69,
	0x4e, 0x5e, 0x66, 0x28, 0x85, 0x91, 0x04, 0xd2, 0xc8, 0xd0, 0x68, 0xf2, 0x6a, 0x30, 0x26, 0x03,
	0x3f, 0xd1, 0xfa, 0x99, 0xa1, 0xff, 0xc2, 0xb8, 0x46, 0x5b, 0x09, 0x24, 0x32, 0x21, 0xb9, 0xea,
	0xe3, 0x27, 0x59, 0xfa, 0x1f, 0xd0, 0x4b, 0x78, 0x24, 0xc2, 0xc9, 0x97, 0x2c, 0x2d, 0xc1, 0xc4,
	0x15, 0xc2, 0x68, 0xf2, 0x35, 0x9b, 0x48, 0x45, 0x02, 0x3a, 0x64, 0x83, 0x75, 0x3f, 0x0d, 0xd1,
	0x11, 0xc8, 0x6b, 0xb4, 0x4b, 0xab, 0xc8, 0x35, 0x79, 0x93, 0xeb, 0x67, 0x6d, 0x43, 0xae, 0x2a,
	0x5c, 0x22, 0x79, 0xea, 0x46, 0x59, 0x6b, 0xb4, 0xbe, 0x51, 0x0c, 0x45, 0x20, 0xad, 0xd4, 0xe4,
	0x99, 0x4b, 0xff, 0x87, 0x49, 0x8d, 0x16, 0x6b, 0x2a, 0x30, 0xd5, 0x5a, 0x68, 0xe2, 0x0f, 0x22,
	0xea, 0xb9, 0x4b, 0x6f, 0xc0, 0xb5, 0xcb, 0x54, 0x2c, 0x3d, 0x18, 0x78, 0xe1, 0x26, 0xcf, 0x5c,
	0x31, 0x4c, 0xa2, 0xa8, 0x73, 0x4d, 0xce, 0x5d, 0x7a, 0x1d, 0x4a, 0x97, 0x3d, 0xfe, 0xa2, 0x7f,
	0xb8, 0x49, 0x58, 0x22, 0xb4, 0xcc, 0xf7, 0x15, 0xd7, 0x9a, 0x7c, 0xce, 0x27, 0x7f, 0x22, 0x0c,
	0x14, 0x92, 0x0f, 0xc3, 0x49, 0x1e, 0x35, 0xc4, 0xd0, 0x2a, 0xbe, 0x62, 0x1b, 0x1c, 0x6b, 0x81,
	0x4f, 0x5e, 0x43, 0x92, 0x47, 0x42, 0xe8, 0x70, 0x90, 0xc7, 0x2f, 0x48, 0x9e, 0xf0, 0x87, 0x69,
	0x70, 0xad, 0x59, 0x95, 0x93, 0x6f, 0x85, 0xa5, 0x3b, 0xc7, 0xa7, 0x5e, 0xea, 0xe4, 0xd4, 0x4b,
	0x5d, 0x9c, 0x7a, 0xce, 0x93, 0x9e, 0xe7, 0xbc, 0xed, 0x79, 0xce, 0x51, 0xcf, 0x73, 0x8e, 0x7b,
	0x9e, 0xf3, 0xbd, 0xe7, 0x39, 0xe7, 0x3d, 0x2f, 0x75, 0xd1, 0xf3, 0x9c, 0xc3, 0x33, 0x2f, 0x75,
	0x7c, 0xe6, 0xa5, 0x4e, 0xce, 0xbc, 0xd4, 0x03, 0x37, 0xbe, 0xc5, 0xf6, 0xda, 0x5a, 0x2e, 0x3e,
	0xcf, 0xdb, 0xbf, 0x03, 0x00, 0x00, 0xff, 0xff, 0xf9, 0xc5, 0xba, 0xb6, 0xc4, 0x03, 0x00, 0x00,
}

func (x DataType) String() string {
	s, ok := DataType_name[int32(x)]
	if ok {
		return s
	}
	return strconv.Itoa(int(x))
}
func (x PatternType) String() string {
	s, ok := PatternType_name[int32(x)]
	if ok {
		return s
	}
	return strconv.Itoa(int(x))
}
func (x SemanticType) String() string {
	s, ok := SemanticType_name[int32(x)]
	if ok {
		return s
	}
	return strconv.Itoa(int(x))
}
func (this *UInt128) Equal(that interface{}) bool {
	if that == nil {
		return this == nil
	}

	that1, ok := that.(*UInt128)
	if !ok {
		that2, ok := that.(UInt128)
		if ok {
			that1 = &that2
		} else {
			return false
		}
	}
	if that1 == nil {
		return this == nil
	} else if this == nil {
		return false
	}
	if this.Low != that1.Low {
		return false
	}
	if this.High != that1.High {
		return false
	}
	return true
}
func (this *UInt128) GoString() string {
	if this == nil {
		return "nil"
	}
	s := make([]string, 0, 6)
	s = append(s, "&typespb.UInt128{")
	s = append(s, "Low: "+fmt.Sprintf("%#v", this.Low)+",\n")
	s = append(s, "High: "+fmt.Sprintf("%#v", this.High)+",\n")
	s = append(s, "}")
	return strings.Join(s, "")
}
func valueToGoStringTypes(v interface{}, typ string) string {
	rv := reflect.ValueOf(v)
	if rv.IsNil() {
		return "nil"
	}
	pv := reflect.Indirect(rv).Interface()
	return fmt.Sprintf("func(v %v) *%v { return &v } ( %#v )", typ, typ, pv)
}
func (m *UInt128) Marshal() (dAtA []byte, err error) {
	size := m.Size()
	dAtA = make([]byte, size)
	n, err := m.MarshalToSizedBuffer(dAtA[:size])
	if err != nil {
		return nil, err
	}
	return dAtA[:n], nil
}

func (m *UInt128) MarshalTo(dAtA []byte) (int, error) {
	size := m.Size()
	return m.MarshalToSizedBuffer(dAtA[:size])
}

func (m *UInt128) MarshalToSizedBuffer(dAtA []byte) (int, error) {
	i := len(dAtA)
	_ = i
	var l int
	_ = l
	if m.High != 0 {
		i = encodeVarintTypes(dAtA, i, uint64(m.High))
		i--
		dAtA[i] = 0x10
	}
	if m.Low != 0 {
		i = encodeVarintTypes(dAtA, i, uint64(m.Low))
		i--
		dAtA[i] = 0x8
	}
	return len(dAtA) - i, nil
}

func encodeVarintTypes(dAtA []byte, offset int, v uint64) int {
	offset -= sovTypes(v)
	base := offset
	for v >= 1<<7 {
		dAtA[offset] = uint8(v&0x7f | 0x80)
		v >>= 7
		offset++
	}
	dAtA[offset] = uint8(v)
	return base
}
func (m *UInt128) Size() (n int) {
	if m == nil {
		return 0
	}
	var l int
	_ = l
	if m.Low != 0 {
		n += 1 + sovTypes(uint64(m.Low))
	}
	if m.High != 0 {
		n += 1 + sovTypes(uint64(m.High))
	}
	return n
}

func sovTypes(x uint64) (n int) {
	return (math_bits.Len64(x|1) + 6) / 7
}
func sozTypes(x uint64) (n int) {
	return sovTypes(uint64((x << 1) ^ uint64((int64(x) >> 63))))
}
func (this *UInt128) String() string {
	if this == nil {
		return "nil"
	}
	s := strings.Join([]string{`&UInt128{`,
		`Low:` + fmt.Sprintf("%v", this.Low) + `,`,
		`High:` + fmt.Sprintf("%v", this.High) + `,`,
		`}`,
	}, "")
	return s
}
func valueToStringTypes(v interface{}) string {
	rv := reflect.ValueOf(v)
	if rv.IsNil() {
		return "nil"
	}
	pv := reflect.Indirect(rv).Interface()
	return fmt.Sprintf("*%v", pv)
}
func (m *UInt128) Unmarshal(dAtA []byte) error {
	l := len(dAtA)
	iNdEx := 0
	for iNdEx < l {
		preIndex := iNdEx
		var wire uint64
		for shift := uint(0); ; shift += 7 {
			if shift >= 64 {
				return ErrIntOverflowTypes
			}
			if iNdEx >= l {
				return io.ErrUnexpectedEOF
			}
			b := dAtA[iNdEx]
			iNdEx++
			wire |= uint64(b&0x7F) << shift
			if b < 0x80 {
				break
			}
		}
		fieldNum := int32(wire >> 3)
		wireType := int(wire & 0x7)
		if wireType == 4 {
			return fmt.Errorf("proto: UInt128: wiretype end group for non-group")
		}
		if fieldNum <= 0 {
			return fmt.Errorf("proto: UInt128: illegal tag %d (wire type %d)", fieldNum, wire)
		}
		switch fieldNum {
		case 1:
			if wireType != 0 {
				return fmt.Errorf("proto: wrong wireType = %d for field Low", wireType)
			}
			m.Low = 0
			for shift := uint(0); ; shift += 7 {
				if shift >= 64 {
					return ErrIntOverflowTypes
				}
				if iNdEx >= l {
					return io.ErrUnexpectedEOF
				}
				b := dAtA[iNdEx]
				iNdEx++
				m.Low |= uint64(b&0x7F) << shift
				if b < 0x80 {
					break
				}
			}
		case 2:
			if wireType != 0 {
				return fmt.Errorf("proto: wrong wireType = %d for field High", wireType)
			}
			m.High = 0
			for shift := uint(0); ; shift += 7 {
				if shift >= 64 {
					return ErrIntOverflowTypes
				}
				if iNdEx >= l {
					return io.ErrUnexpectedEOF
				}
				b := dAtA[iNdEx]
				iNdEx++
				m.High |= uint64(b&0x7F) << shift
				if b < 0x80 {
					break
				}
			}
		default:
			iNdEx = preIndex
			skippy, err := skipTypes(dAtA[iNdEx:])
			if err != nil {
				return err
			}
			if skippy < 0 {
				return ErrInvalidLengthTypes
			}
			if (iNdEx + skippy) < 0 {
				return ErrInvalidLengthTypes
			}
			if (iNdEx + skippy) > l {
				return io.ErrUnexpectedEOF
			}
			iNdEx += skippy
		}
	}

	if iNdEx > l {
		return io.ErrUnexpectedEOF
	}
	return nil
}
func skipTypes(dAtA []byte) (n int, err error) {
	l := len(dAtA)
	iNdEx := 0
	depth := 0
	for iNdEx < l {
		var wire uint64
		for shift := uint(0); ; shift += 7 {
			if shift >= 64 {
				return 0, ErrIntOverflowTypes
			}
			if iNdEx >= l {
				return 0, io.ErrUnexpectedEOF
			}
			b := dAtA[iNdEx]
			iNdEx++
			wire |= (uint64(b) & 0x7F) << shift
			if b < 0x80 {
				break
			}
		}
		wireType := int(wire & 0x7)
		switch wireType {
		case 0:
			for shift := uint(0); ; shift += 7 {
				if shift >= 64 {
					return 0, ErrIntOverflowTypes
				}
				if iNdEx >= l {
					return 0, io.ErrUnexpectedEOF
				}
				iNdEx++
				if dAtA[iNdEx-1] < 0x80 {
					break
				}
			}
		case 1:
			iNdEx += 8
		case 2:
			var length int
			for shift := uint(0); ; shift += 7 {
				if shift >= 64 {
					return 0, ErrIntOverflowTypes
				}
				if iNdEx >= l {
					return 0, io.ErrUnexpectedEOF
				}
				b := dAtA[iNdEx]
				iNdEx++
				length |= (int(b) & 0x7F) << shift
				if b < 0x80 {
					break
				}
			}
			if length < 0 {
				return 0, ErrInvalidLengthTypes
			}
			iNdEx += length
		case 3:
			depth++
		case 4:
			if depth == 0 {
				return 0, ErrUnexpectedEndOfGroupTypes
			}
			depth--
		case 5:
			iNdEx += 4
		default:
			return 0, fmt.Errorf("proto: illegal wireType %d", wireType)
		}
		if iNdEx < 0 {
			return 0, ErrInvalidLengthTypes
		}
		if depth == 0 {
			return iNdEx, nil
		}
	}
	return 0, io.ErrUnexpectedEOF
}

var (
	ErrInvalidLengthTypes        = fmt.Errorf("proto: negative length found during unmarshaling")
	ErrIntOverflowTypes          = fmt.Errorf("proto: integer overflow")
	ErrUnexpectedEndOfGroupTypes = fmt.Errorf("proto: unexpected end of group")
)
