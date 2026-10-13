package cn.wch.ch37xdriver;

import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.hardware.usb.UsbManager;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {

	private static final String ACTION_USB_PERMISSION = "cn.wch.ch37xdriver.USB_PERMISSION";
	private boolean READ_ENABLE = false;
	private Handler handler;
	private EditText readText;
	private EditText writeText;
	private TextView DeviceInfo, DeviceState;
	private EditText DownCount, UpCount;
	private Button DownBtn, Up_ClearBtn, Down_ClearBtn, openButton;
	private boolean isOpen;
	private int Upload_count, Download_count;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);

		initUI();

		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);// 保持常亮的屏幕的状态
		MyApp.ch37xdrv = new CH37xDriver(
				(UsbManager) getSystemService(Context.USB_SERVICE),
				MainActivity.this, ACTION_USB_PERMISSION);
		if (!MyApp.ch37xdrv.UsbFeatureSupported())// 判断系统是否支持USB HOST
		{
			Dialog dialog = new AlertDialog.Builder(MainActivity.this)
					.setTitle("提示")
					.setMessage("您的手机不支持USB HOST，请更换其他手机再试！")
					.setPositiveButton("确认",
							new DialogInterface.OnClickListener() {

								@Override
								public void onClick(DialogInterface arg0,
										int arg1) {
									System.exit(0);
								}
							}).create();
			dialog.setCanceledOnTouchOutside(false);
			dialog.show();
		}
		handler = new Handler() {//处理接收到的数据的handler
			@Override
			public void handleMessage(Message msg) {
				super.handleMessage(msg);
				Upload_count += msg.what;
				byte[] recv = (byte[]) msg.obj;
				String append_String = toHexString(recv, msg.what);
				readText.append(append_String);
				UpCount.setText(String.valueOf(Upload_count));
			}
		};

		openButton.setOnClickListener(new View.OnClickListener() {

			@Override
			public void onClick(View v) {
				if (!isOpen) {
					if (MyApp.ch37xdrv.OpenDevice()) {
						Toast.makeText(MainActivity.this, "设备打开成功!",
								Toast.LENGTH_SHORT).show();
						isOpen = true;
						openButton.setText("关闭");
						READ_ENABLE = true;
						new ReadThread().start();
						DeviceInfo.setText("厂商ID:"
								+ Integer.toHexString(MyApp.ch37xdrv.GetPID())
								+ "，设备ID:"
								+ Integer.toHexString(MyApp.ch37xdrv.GetVID()));
						DeviceState.setText("设备已打开");
						DownBtn.setEnabled(true);
					} else {
						Toast.makeText(MainActivity.this, "设备打开失败!",
								Toast.LENGTH_SHORT).show();
					}
				} else {
					isOpen = false;
					READ_ENABLE = false;
					openButton.setText("打开");
					DeviceInfo.setText("设备号未知");
					DeviceState.setText("设备已关闭");
					DownBtn.setEnabled(false);
					MyApp.ch37xdrv.CloseDevice();
				}
			}
		});
		// 发送数据
		DownBtn.setOnClickListener(new View.OnClickListener() {

			@Override
			public void onClick(View v) {
				byte[] send_buff;
				send_buff = toByteArray(writeText.getText().toString());
				int send_length = MyApp.ch37xdrv.WriteData(send_buff,
						send_buff.length);
				if (send_length < 0) {
					Toast.makeText(MainActivity.this, "数据发送失败!",
							Toast.LENGTH_SHORT).show();
				} else {
					Download_count += send_length;
					DownCount.setText(String.valueOf(Download_count));
				}
			}
		});

	}

	// 初始化界面
	public void initUI() {
		Upload_count = 0;
		Download_count = 0;
		isOpen = false;
		openButton = (Button) findViewById(R.id.OpenDevice);
		DownBtn = (Button) findViewById(R.id.DownBtn);
		Up_ClearBtn = (Button) findViewById(R.id.ClearBtn_Up);
		Down_ClearBtn = (Button) findViewById(R.id.ClearBtn_Down);

		DeviceInfo = (TextView) findViewById(R.id.DeviceInfo);
		DeviceState = (TextView) findViewById(R.id.DeviceState);
		DownCount = (EditText) findViewById(R.id.DownCount);
		UpCount = (EditText) findViewById(R.id.UpCount);
		readText = (EditText) findViewById(R.id.ReadValues);
		writeText = (EditText) findViewById(R.id.WriteValues);
		DownBtn.setEnabled(false);
		DeviceInfo.setText("厂商ID：未知，设备ID：未知");
		DeviceState.setText("设备未打开");
		Up_ClearBtn.setOnClickListener(new View.OnClickListener() {

			@Override
			public void onClick(View v) {
				readText.setText("");
				UpCount.setText("0");
				Upload_count = 0;
			}
		});

		Down_ClearBtn.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				writeText.setText("");
				DownCount.setText("0");
				Download_count = 0;
			}
		});

	}

	// 读取线程，将接受到的数据通过消息发送到接收框中
	private class ReadThread extends Thread {

		public void run() {

			while (READ_ENABLE) {
				Message msg = Message.obtain();
				int read_length;
				byte[] read_buff = new byte[64];
				if (MyApp.ch37xdrv != null) {
					read_length = MyApp.ch37xdrv.ReadData(read_buff, 64);
					if (read_length > 0) {
						msg.what = read_length;
						msg.obj = read_buff;
						handler.sendMessage(msg);
					}
				}
			}
		}
	}

	/**
	 * 将byte[]数组转化为String类型
	 * 
	 * @param arg
	 *            需要转换的byte[]数组
	 * @param length
	 *            需要转换的数组长度
	 * @return 转换后的String队形
	 */
	private String toHexString(byte[] arg, int length) {
		String result = new String();
		if (arg != null) {
			for (int i = 0; i < length; i++) {
				result = result
						+ (Integer.toHexString(
								arg[i] < 0 ? arg[i] + 256 : arg[i]).length() == 1 ? "0"
								+ Integer.toHexString(arg[i] < 0 ? arg[i] + 256
										: arg[i])
								: Integer.toHexString(arg[i] < 0 ? arg[i] + 256
										: arg[i])) + " ";
			}
			return result;
		}
		return "";
	}

	/**
	 * 将String转化为byte[]数组
	 * 
	 * @param arg
	 *            需要转换的String对象
	 * @return 转换后的byte[]数组
	 */
	private byte[] toByteArray(String arg) {
		if (arg != null) {
			/* 1.先去除String中的' '，然后将String转换为char数组 */
			char[] NewArray = new char[1000];
			char[] array = arg.toCharArray();
			int length = 0;
			for (int i = 0; i < array.length; i++) {
				if (array[i] != ' ') {
					NewArray[length] = array[i];
					length++;
				}
			}
			/* 将char数组中的值转成一个实际的十进制数组 */
			int EvenLength = (length % 2 == 0) ? length : length + 1;
			if (EvenLength != 0) {
				int[] data = new int[EvenLength];
				data[EvenLength - 1] = 0;
				for (int i = 0; i < length; i++) {
					if (NewArray[i] >= '0' && NewArray[i] <= '9') {
						data[i] = NewArray[i] - '0';
					} else if (NewArray[i] >= 'a' && NewArray[i] <= 'f') {
						data[i] = NewArray[i] - 'a' + 10;
					} else if (NewArray[i] >= 'A' && NewArray[i] <= 'F') {
						data[i] = NewArray[i] - 'A' + 10;
					}
				}
				/* 将 每个char的值每两个组成一个16进制数据 */
				byte[] byteArray = new byte[EvenLength / 2];
				for (int i = 0; i < EvenLength / 2; i++) {
					byteArray[i] = (byte) (data[i * 2] * 16 + data[i * 2 + 1]);
				}
				return byteArray;
			}
		}
		return new byte[] {};
	}

}
