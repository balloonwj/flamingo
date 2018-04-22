package org.hootina.platform.utils;
import java.io.PrintWriter;

/**
 * ��c����ͨ��(java��client��c/c++��server������һ���ṹ)
 * 
 * @author kingfish
 * @version 1.0
 */


public class Employee {
	private PrintWriter out = null; 
	private byte[] buf = new byte[28]; // Ϊ˵�����⣬������С���¼��п�������

	/**
	 * ��intתΪ���ֽ���ǰ�����ֽ��ں��byte����
	 */
	public static byte[] tolh(int n) {
		byte[] b = new byte[4];
		b[0] = (byte) (n & 0xff);
		b[1] = (byte) (n >> 8 & 0xff);
		b[2] = (byte) (n >> 16 & 0xff);
		b[3] = (byte) (n >> 24 & 0xff);
		return b;
	}
	/**
	 * ��byte����ת����String
	 */
	public static String toStr(byte[] valArr,int maxLen) {
		int index = 0;
		while(index < valArr.length && index < maxLen) {
			if(valArr[index] == 0) {
				break;
			}
			index++;
		}
		byte[] temp = new byte[index];
		System.arraycopy(valArr, 0, temp, 0, index);
		return new String(temp);
	}
	
	/**
	 * �����ֽ���ǰ���ֽ��ں��byte����תΪint��
	 */
	public static int vtolh(byte[] bArr) {
		int n = 0;
		for(int i=0;i<bArr.length&&i<4;i++){
			int left = i*8;
			n+= (bArr[i] << left);
		}
		return n;
	}
	public String name = "";
	public int id = 0;
	public float salary = 0;
	/**
	 * ��floatתΪ���ֽ���ǰ�����ֽ��ں��byte����
	 */
	private static byte[] tolh(float f) {
		return tolh(Float.floatToRawIntBits(f));
	}
	
	public static Employee getEmployee(byte[] bArr) {
		String name = "";
		int id = 0;
		float salary = 0;
		
		byte[] temp = new byte[20];
		name = toStr(bArr,20);
		
		System.arraycopy(bArr, 20, temp, 0, 4);
		id = vtolh(temp);
		
		return new Employee(name, id, salary);
		

	}
	/**
	 * ���첢ת��
	 */
	public Employee(String name, int id, float salary) {
		this.name = name;
		this.id = id;
		this.salary = salary;
		
		byte[] temp = name.getBytes();
		System.arraycopy(temp, 0, buf, 0, temp.length);

		temp = tolh(id);
		System.arraycopy(temp, 0, buf, 20, temp.length);

		temp = tolh(salary);
		System.arraycopy(temp, 0, buf, 24, temp.length);
	}
	
	/**
	 * ����Ҫ���͵�����
	 */
	public byte[] getbuf() {
		return buf;
	}

	/**
	 * ���Ͳ���
	 */
	
} // end 